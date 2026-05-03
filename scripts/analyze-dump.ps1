#!/usr/bin/env pwsh
# Minimal native minidump analyzer - extracts the exception record, identifies
# which loaded module contains the faulting address, and walks the saved
# thread context's RSP to print return addresses + their owning modules.
#
# Usage: pwsh -File analyze-dump.ps1 <path-to-.dmp>
#
# Why this exists: cdb/windbg aren't installed and pulling them in via winget
# blew up. The minidump format is documented and the bits we need (exception
# stream, module list, faulting thread context, stack memory) are simple
# fixed-layout records, so we just read them directly.

param(
    [Parameter(Mandatory=$true, Position=0)]
    [string]$DumpPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $DumpPath)) {
    Write-Error "Dump not found: $DumpPath"
    exit 1
}

$bytes = [System.IO.File]::ReadAllBytes($DumpPath)
$ms = New-Object System.IO.MemoryStream(,$bytes)
$br = New-Object System.IO.BinaryReader($ms)

function Read-MinidumpString([int]$rva) {
    $ms.Position = $rva
    $len = $br.ReadUInt32()       # length in bytes
    $buf = $br.ReadBytes($len)
    return [System.Text.Encoding]::Unicode.GetString($buf)
}

# MINIDUMP_HEADER
$ms.Position = 0
$sig = $br.ReadUInt32()
if ($sig -ne 0x504D444D) { Write-Error "Not a minidump (sig=0x$($sig.ToString('x')))"; exit 1 }
$ver = $br.ReadUInt32()
$numStreams = $br.ReadUInt32()
$dirRva = $br.ReadUInt32()

# Read stream directory
$streams = @{}
for ($i = 0; $i -lt $numStreams; $i++) {
    $ms.Position = $dirRva + ($i * 12)
    $streamType = $br.ReadUInt32()
    $dataSize   = $br.ReadUInt32()
    $rva        = $br.ReadUInt32()
    $streams[$streamType] = @{ Size = $dataSize; Rva = $rva }
}

[uint32]$STREAM_THREADLIST = 3
[uint32]$STREAM_MODULELIST = 4
[uint32]$STREAM_EXCEPTION  = 6
[uint32]$STREAM_MEMORY64   = 9   # MemoryListStream; Memory64 is 16
[uint32]$STREAM_MEMORY64_FULL = 16

Write-Output ("Streams in dump: " + (($streams.Keys | Sort-Object) -join ', '))
Write-Output ""

# --- ModuleList ---
$modules = @()
if ($streams.ContainsKey($STREAM_MODULELIST)) {
    $ms.Position = $streams[$STREAM_MODULELIST].Rva
    $numModules = $br.ReadUInt32()
    for ($i = 0; $i -lt $numModules; $i++) {
        $base    = $br.ReadUInt64()
        $size    = $br.ReadUInt32()
        $null    = $br.ReadUInt32()  # CheckSum
        $null    = $br.ReadUInt32()  # TimeDateStamp
        $nameRva = $br.ReadUInt32()
        $null    = $br.ReadBytes(52) # VS_FIXEDFILEINFO
        $null    = $br.ReadBytes(8)  # CvRecord MINIDUMP_LOCATION_DESCRIPTOR
        $null    = $br.ReadBytes(8)  # MiscRecord
        $null    = $br.ReadUInt64()  # Reserved0
        $null    = $br.ReadUInt64()  # Reserved1

        $savePos = $ms.Position
        $name = Read-MinidumpString $nameRva
        $ms.Position = $savePos

        $modules += [pscustomobject]@{
            Base = $base
            Size = $size
            End  = $base + $size
            Name = $name
        }
    }
}

function Get-ModuleAt([UInt64]$addr) {
    foreach ($m in $modules) {
        if ($addr -ge $m.Base -and $addr -lt $m.End) { return $m }
    }
    return $null
}

function Format-Addr([UInt64]$addr) {
    $m = Get-ModuleAt $addr
    if ($null -ne $m) {
        $offset = $addr - $m.Base
        $shortName = Split-Path -Leaf $m.Name
        return ("0x{0:x16}  {1}+0x{2:x}" -f $addr, $shortName, $offset)
    }
    return ("0x{0:x16}  (unmapped)" -f $addr)
}

# --- ExceptionStream ---
if (-not $streams.ContainsKey($STREAM_EXCEPTION)) {
    Write-Output "No exception stream in dump."
    exit 0
}

$ms.Position = $streams[$STREAM_EXCEPTION].Rva
$threadId    = $br.ReadUInt32()
$null        = $br.ReadUInt32()  # __alignment
# MINIDUMP_EXCEPTION
$exCode      = $br.ReadUInt32()
$exFlags     = $br.ReadUInt32()
$exRecord    = $br.ReadUInt64()
$exAddr      = $br.ReadUInt64()
$numParams   = $br.ReadUInt32()
$null        = $br.ReadUInt32()  # __unusedAlignment
$exInfo = @()
for ($p = 0; $p -lt 15; $p++) { $exInfo += $br.ReadUInt64() }
# Trailing MINIDUMP_LOCATION_DESCRIPTOR for ThreadContext
$ctxSize = $br.ReadUInt32()
$ctxRva  = $br.ReadUInt32()

$exNames = @{
    0xC0000005 = "EXCEPTION_ACCESS_VIOLATION"
    0xC000001D = "EXCEPTION_ILLEGAL_INSTRUCTION"
    0xC0000094 = "EXCEPTION_INT_DIVIDE_BY_ZERO"
    0xC0000095 = "EXCEPTION_INT_OVERFLOW"
    0xC00000FD = "EXCEPTION_STACK_OVERFLOW"
    0xC0000409 = "STATUS_STACK_BUFFER_OVERRUN"
    0xC000041D = "FAST_FAIL"
    0xE06D7363 = "C++ EH (throw)"
    0x80000003 = "BREAKPOINT"
}

Write-Output "=== Exception ==="
$exName = $exNames[[uint32]$exCode]
if (-not $exName) { $exName = "Unknown" }
Write-Output ("Code:    0x{0:x8}  ({1})" -f $exCode, $exName)
Write-Output ("Flags:   0x{0:x8}" -f $exFlags)
Write-Output ("Address: " + (Format-Addr $exAddr))
Write-Output ("Thread:  $threadId")
if ($exCode -eq 0xC0000005 -and $numParams -ge 2) {
    $kind = $exInfo[0]
    $where = $exInfo[1]
    $kindName = switch ($kind) { 0 {'read'} 1 {'write'} 8 {'execute'} default {"$kind"} }
    Write-Output ("AV: $kindName at 0x{0:x16}" -f $where)
}
Write-Output ""

# --- Walk faulting thread's stack (return addresses on stack, scan for code pointers) ---
# Read CONTEXT (x64). Layout: P1Home..P6Home (6*8), ContextFlags(4), MxCsr(4), seg regs...
# We just need RIP and RSP.
# Offsets from start of CONTEXT for x64:
#   0xF8 = Rsp
#   0xF8 = Rsp (offset confirmed)
#   0xF8 actually: let's compute. Standard CONTEXT_x64 offsets:
#     0x00: P1-P6 Home (48 bytes)
#     0x30: ContextFlags(4) MxCsr(4)
#     0x38: SegCs..SegSs (6*2=12) + EFlags(4) = 16 bytes, ends at 0x48
#     0x48: Dr0..Dr7 (6*8 = 48) ends at 0x78
#     0x78: Rax,Rcx,Rdx,Rbx (4*8=32) ends at 0x98
#     0x98: Rsp (8) ends at 0xA0
#     0xA0: Rbp,Rsi,Rdi,R8..R15 (11*8=88) ends at 0xF8
#     0xF8: Rip (8)

if ($ctxSize -gt 0 -and $ctxRva -gt 0) {
    $ms.Position = $ctxRva + 0x98
    $rsp = $br.ReadUInt64()
    $ms.Position = $ctxRva + 0xF8
    $rip = $br.ReadUInt64()

    Write-Output "=== Faulting context ==="
    Write-Output ("RIP: " + (Format-Addr $rip))
    Write-Output ("RSP: 0x{0:x16}" -f $rsp)
    Write-Output ""

    # Build memory range table from whichever stream is present.
    # MemoryListStream (5): NumberOfMemoryRanges(4), then [VA(8), DataSize(4), Rva(4)] per entry.
    # Memory64ListStream (16): NumberOfMemoryRanges(8), BaseRva(8), then [VA(8), Size(8)] (file-contiguous).
    [uint32]$STREAM_MEMORYLIST = 5
    $ranges = @()
    if ($streams.ContainsKey($STREAM_MEMORYLIST)) {
        $ms.Position = $streams[$STREAM_MEMORYLIST].Rva
        $numRanges = $br.ReadUInt32()
        for ($r = 0; $r -lt $numRanges; $r++) {
            $start    = $br.ReadUInt64()
            $dataSz   = $br.ReadUInt32()
            $rva      = $br.ReadUInt32()
            $ranges += [pscustomobject]@{ Start = $start; End = $start + $dataSz; Rva = [UInt64]$rva }
        }
    } elseif ($streams.ContainsKey($STREAM_MEMORY64_FULL)) {
        $ms.Position = $streams[$STREAM_MEMORY64_FULL].Rva
        $numRanges = $br.ReadUInt64()
        $baseRva   = $br.ReadUInt64()
        $cursor = [UInt64]$baseRva
        for ($r = 0; $r -lt $numRanges; $r++) {
            $start = $br.ReadUInt64()
            $size  = $br.ReadUInt64()
            $ranges += [pscustomobject]@{ Start = $start; End = $start + $size; Rva = $cursor }
            $cursor = [UInt64]($cursor + $size)
        }
    }

    function Read-DumpBytes([UInt64]$va, [int]$count) {
        foreach ($r in $ranges) {
            if ($va -ge $r.Start -and ($va + [UInt64]$count) -le $r.End) {
                $offset = [UInt64]($r.Rva + ($va - $r.Start))
                if ($offset -gt [int]::MaxValue) { return $null }
                $ms.Position = [int]$offset
                return $br.ReadBytes($count)
            }
        }
        return $null
    }

    if ($ranges.Count -gt 0) {
        # Disassembly bytes at faulting RIP (raw hex; user can decode externally).
        $faultBytes = Read-DumpBytes $rip 32
        if ($null -ne $faultBytes) {
            $hex = ($faultBytes | ForEach-Object { '{0:x2}' -f $_ }) -join ' '
            Write-Output "Bytes at RIP: $hex"
            Write-Output ""
        }

        # Scan ~16 KB of stack for QWORDs that point into known modules' executable regions.
        $scanBytes = Read-DumpBytes $rsp 16384
        if ($null -ne $scanBytes) {
            Write-Output "=== Code-pointer candidates on faulting thread's stack ==="
            Write-Output "(qwords at [RSP..RSP+0x4000] that fall inside a loaded module)"
            $found = @{}
            for ($off = 0; $off -lt ($scanBytes.Length - 8); $off += 8) {
                $q = [BitConverter]::ToUInt64($scanBytes, $off)
                $m = Get-ModuleAt $q
                if ($null -ne $m) {
                    $key = "$([Math]::Floor($q / 16))"
                    if (-not $found.ContainsKey($key)) {
                        $found[$key] = $true
                        Write-Output ("  [RSP+0x{0:x4}]  {1}" -f $off, (Format-Addr $q))
                    }
                }
            }
        } else {
            Write-Output "(stack memory not in dump's memory ranges)"
        }
    } else {
        Write-Output "(no memory stream - cannot read stack)"
    }
}

Write-Output ""
Write-Output "=== Plugin/REFramework modules in dump ==="
$modules | Where-Object { $_.Name -match "(?i)RE9HeadTracking|reframework|dinput8" } | ForEach-Object {
    Write-Output ("  {0}  base=0x{1:x16}  size=0x{2:x}" -f (Split-Path -Leaf $_.Name), $_.Base, $_.Size)
}
