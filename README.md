# RE9 Head Tracking

![Mod GIF](https://github.com/itsloopyo/resident-evil-requiem-headtracking/raw/main/assets/readme-clip.gif)

An unofficial, flatscreen head tracking mod for Resident Evil Requiem - no VR headset required. Use a webcam, phone, or any OpenTrack-compatible tracker to look around the environment by moving your head while aiming with your mouse.

## Features

- **Decoupled look and aim** - head tracking moves the camera; aim stays on your mouse/controller
- **6DOF positional tracking** - lean and peek with head position

## Requirements

- [Resident Evil Requiem](https://store.steampowered.com/) (Steam)
- [OpenTrack](https://github.com/opentrack/opentrack) or a compatible head tracking app (smartphone, webcam, or dedicated hardware)
- Windows 10/11 (64-bit)

## Installation

1. Download the latest release from the [Releases page](https://github.com/itsloopyo/resident-evil-requiem-headtracking/releases)
2. Extract the ZIP anywhere
3. Double-click `install.cmd`
4. The installer auto-detects your game and installs REFramework if needed
5. Configure OpenTrack to output UDP to `127.0.0.1:4242`
6. Launch the game - head tracking is enabled automatically

The installer finds your game via Steam registry lookup. If it can't find the game:
- Set the `RE9_PATH` environment variable to your game folder, or
- Run from command prompt: `install.cmd "D:\Games\RE9"`

### Manual Installation

1. Install [REFramework](https://github.com/praydog/REFramework-nightly/releases) for RE9 (extract to game root)
2. Copy `RE9HeadTracking.dll` and `HeadTracking.ini` to `<game>/reframework/plugins/`

## Setting Up OpenTrack

1. Download and install [OpenTrack](https://github.com/opentrack/opentrack/releases)
2. Configure your tracker as input
3. Set output to **UDP over network**
4. Host: `127.0.0.1`, Port: `4242`
5. Start tracking before launching the game

### Webcam Setup

No special hardware needed - OpenTrack's built-in **neuralnet tracker** uses any webcam for 6DOF face tracking.

1. In OpenTrack, set the input to **neuralnet tracker**
2. Select your webcam in the tracker settings
3. Set output to **UDP over network** (`127.0.0.1:4242`)
4. Start tracking before launching the game
5. Recenter in OpenTrack via its hotkey, and press **Home** in-game to recenter the mod as needed

### Phone App Setup

If your phone app sends a sufficiently filtered signal (built-in smoothing, stable sample rate), you can send directly to port 4242 without needing OpenTrack on PC. The mod includes interpolation for network jitter.

1. Install an OpenTrack-compatible head tracking app
2. Configure it to send to your PC's IP on port 4242 (run `ipconfig` to find it)
3. Set the protocol to OpenTrack/UDP

**With OpenTrack (optional):** If you want curve mapping or visual preview, route through OpenTrack. Set OpenTrack's input to "UDP over network" on a different port (e.g. 5252), point your phone app at that port, and set OpenTrack's output to `127.0.0.1:4242`. Make sure your firewall allows incoming UDP on the input port.

## Controls

| Key | Action |
|-----|--------|
| **Home** | Recenter view |
| **End** | Toggle head tracking on/off |
| **Page Up** | Toggle positional tracking on/off |
| **Page Down** | Toggle world/local yaw mode |

## Configuration

The mod creates a config file at `reframework/plugins/HeadTracking.ini` on first run. Edit it to customize:

```ini
[Network]
UDPPort=4242

[Sensitivity]
YawMultiplier=1.0           ; Horizontal rotation (0.1-5.0)
PitchMultiplier=1.0         ; Vertical rotation (0.1-5.0)
RollMultiplier=1.0          ; Head tilt (0.0-2.0)

[Position]
SensitivityX=2.0            ; Lateral (0.1-10.0)
SensitivityY=2.0            ; Vertical (0.1-10.0)
SensitivityZ=2.0            ; Depth (0.1-10.0)
LimitX=0.30                 ; Max lateral offset in meters
LimitY=0.20                 ; Max vertical offset in meters
LimitZ=0.40                 ; Max forward offset in meters
LimitZBack=0.10             ; Max backward offset (prevents clipping)
Smoothing=0.15              ; Position smoothing (0.0-0.99)
InvertX=true                ; Invert lateral axis
InvertY=false               ; Invert vertical axis
InvertZ=false               ; Invert depth axis
Enabled=true                ; Enable/disable 6DOF

[Hotkeys]
; Virtual key codes (hex)
ToggleKey=0x23              ; End
RecenterKey=0x24            ; Home
PositionToggleKey=0x21      ; Page Up

[General]
AutoEnable=true
WorldSpaceYaw=true          ; true = horizon-locked yaw (default), false = camera-local
```

Delete the file to reset to defaults.

## Troubleshooting

**Mod not loading:**
- Ensure REFramework is installed (`dinput8.dll` in game root)
- Check `reframework/` folder exists with `plugins/RE9HeadTracking.dll` inside
- Try running the game as administrator once

**No tracking response:**
- Verify OpenTrack is running and outputting data
- Check UDP port matches (default 4242)
- Press **End** to enable tracking, **Home** to recenter
- Check firewall isn't blocking UDP port 4242

**Jitter:**
- Increase position smoothing in HeadTracking.ini
- If using a phone app over WiFi, some jitter is expected

**Wrong rotation axis:**
- Adjust sensitivity multipliers or use the Invert settings in the Position section

## Updating

Download the new release and run `install.cmd` again. Your config is preserved.

## Uninstalling

Run `uninstall.cmd` from the release folder. This removes the mod DLLs. REFramework is only removed if it was originally installed by this mod. To force-remove REFramework:

```
uninstall.cmd --force
```

## Building from Source

### Prerequisites

- [CMake](https://cmake.org/) 3.20+
- [Visual Studio 2022](https://visualstudio.microsoft.com/) with C++ desktop workload
- [pixi](https://pixi.sh) task runner

### Build

```bash
git clone --recurse-submodules https://github.com/itsloopyo/resident-evil-requiem-headtracking.git
cd resident-evil-requiem-headtracking

# Build and deploy to game (release)
pixi run install

# Build only (debug)
pixi run build

# Package for release
pixi run package
```

## License

MIT License - see [LICENSE](LICENSE) for details.

## Credits

- [Capcom](https://www.capcom.com/) - Resident Evil Requiem
- [praydog](https://github.com/praydog/REFramework) - REFramework
- [OpenTrack](https://github.com/opentrack/opentrack) - Head tracking software
