<!-- SPDX-License-Identifier: GPL-3.0-or-later - Copyright (C) 2026 Patrick JAILLET -->

<p align="center">
  <img src="docs/banner.svg" alt="ShaderToMP4: turn a Shadertoy shader into video, frame by frame" width="100%">
</p>

<p align="center"><b>English</b> · <a href="README.fr.md">Français</a></p>

<p align="center">
  <a href="https://github.com/ap3demak-rgb/ShaderToMP4/releases/latest"><img alt="Download" src="https://img.shields.io/badge/Download-Windows%20%C2%B7%20Linux%20%C2%B7%20macOS-465AFF?style=for-the-badge"></a>
  &nbsp;
  <a href="https://ap3demak-rgb.github.io/ShaderToMP4"><img alt="Website" src="https://img.shields.io/badge/Website-ShaderToMP4-FF5A96?style=for-the-badge"></a>
</p>

<p align="center">
  <img alt="Version" src="https://img.shields.io/badge/version-1.3.0-FF5A96?style=flat-square">
  <img alt="Platform" src="https://img.shields.io/badge/platform-Windows%20%C2%B7%20Linux%20%C2%B7%20macOS-0078D4?style=flat-square">
  <img alt="Portable" src="https://img.shields.io/badge/portable-no%20installer-3DDC97?style=flat-square">
  <img alt="Languages" src="https://img.shields.io/badge/languages-EN%20%C2%B7%20FR-7C6CFF?style=flat-square">
  <img alt="C++" src="https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square&logo=cplusplus&logoColor=white">
  <a href="LICENSE"><img alt="License" src="https://img.shields.io/badge/license-GPL--3.0--or--later-3DDC97?style=flat-square"></a>
</p>

<p align="center">
  <img src="docs/screenshots/demo.gif" alt="A plasma shader being rendered" width="520">
</p>

**ShaderToMP4** converts a [Shadertoy](https://www.shadertoy.com) shader (*Image* tab) into a video, **frame by frame**, with exact timing (`iTime = frame / fps`). No screen capture, so no stutter and no loss, whatever the power of your graphics card: a shader that runs at 5 frames per second in real time still gives a perfectly smooth video.

One portable program for **Windows, Linux and macOS**, **no installer**, with a modern dark interface in English and French.

---

## Why ShaderToMP4?

| | |
|---|---|
| **Exact timing** | Each frame is rendered at time `n / fps`, then piped straight to ffmpeg: no intermediate file. |
| **Maximum quality** | Up to 16384 pixels wide, ×4 supersampling, motion blur (×4, ×8, ×16), correct BT.709 colour conversion, lossless x264 / x265 if you want it. |
| **Fast** | Rendering and encoding run in parallel, asynchronous GPU read-back, optional hardware encoding (NVENC, AMF, QuickSync, VAAPI, VideoToolbox). |
| **Reliable** | Long renders are made in parts: if anything fails, you resume exactly where you stopped. |
| **Practical** | Built-in preview, presets (YouTube, 4K, vertical 9:16), audio with fades, perfect loop, queue of several shaders, pause and cancel. |
| **Portable** | Nothing to install, on any system. Settings, languages and `ffmpeg` stay in the application folder. |

## The interface

<p align="center">
  <img src="docs/screenshots/en/done.png" alt="ShaderToMP4 after a render" width="860">
</p>

<table>
  <tr>
    <td width="50%"><img src="docs/screenshots/en/empty.png" alt="Illustrated drop zone"></td>
    <td width="50%"><img src="docs/screenshots/en/running.png" alt="Render in progress"></td>
  </tr>
  <tr>
    <td align="center"><sub><b>Drop your shaders</b> in the drop zone, or click “Add”.</sub></td>
    <td align="center"><sub><b>Render in progress</b>: speed, time left, pause and cancel. Progress also shows in the taskbar.</sub></td>
  </tr>
</table>

<p align="center">
  <img src="docs/screenshots/en/settings.png" alt="Video settings" width="860"><br>
  <sub>All video settings in one place, with <b>one-click presets</b> (Draft, YouTube, 4K 60, Vertical) and a tooltip on every field.</sub>
</p>

<p align="center">
  <img src="docs/screenshots/en/list-preview.png" alt="Shader list and preview" width="860"><br>
  <sub>The queue with the status of each shader, and a <b>preview</b> at the time of your choice, without starting ffmpeg.</sub>
</p>

<p align="center">
  <img src="docs/screenshots/en/about.png" alt="About window" width="420"><br>
  <sub>The “About” window (<code>F1</code>): version, copyright, license, contact and website. The globe button switches the language instantly.</sub>
</p>

### On Linux and macOS

Windows keeps its native Direct2D interface. Linux and macOS use a second interface with the same look (dark cards, gradient accent, the same SVG icons), built on GLFW and Dear ImGui and drawn with OpenGL; both interfaces share the same rendering engine, settings and translations.

<p align="center">
  <img src="docs/screenshots/linux/en/main.png" alt="ShaderToMP4 on Linux" width="860"><br>
  <sub>The Linux / macOS interface (screenshot produced by the continuous integration on Linux).</sub>
</p>

## Examples

The [`examples`](examples) folder contains four shaders ready to convert: plasma, rings (“anneaux”), tunnel and aurora (“aurore”).

<p align="center">
  <img src="docs/screenshots/gallery.png" alt="Gallery: plasma, rings, tunnel, aurora" width="860">
</p>

## Quick start

1. **Download** the portable archive for your system from the [Releases](https://github.com/ap3demak-rgb/ShaderToMP4/releases/latest) page and unpack it anywhere (or [build the project](#building)).

   | System | Archive | Run |
   |---|---|---|
   | Windows 10 / 11, 64-bit | `ShaderToMP4-<version>-win64.zip` | `ShaderToMP4.exe` |
   | Linux x86-64 | `ShaderToMP4-<version>-linux-x86_64.tar.gz` | `./ShaderToMP4` |
   | Linux ARM64 | `ShaderToMP4-<version>-linux-aarch64.tar.gz` | `./ShaderToMP4` |
   | macOS 10.13+, Apple Silicon and Intel | `ShaderToMP4-<version>-macos-universal.zip` | `ShaderToMP4.app` |

2. If `ffmpeg` is missing, the program offers to download it (Windows: about 80 MB from [gyan.dev](https://www.gyan.dev/ffmpeg/builds/); Linux: a static build from [BtbN](https://github.com/BtbN/FFmpeg-Builds); SHA-256 checked) and installs it next to itself. On macOS, install it with Homebrew (`brew install ffmpeg`) or place the `ffmpeg` binary next to the `.app`: no source publishes a verifiable SHA-256 for macOS builds, so there is no automatic download there.
3. **Drop** one or more shader files (`.glsl` or `.txt`) in the window, or click **Add**.
4. Adjust the video (or pick a preset), then click **Start**.
5. The video is created next to the shader, or in the folder chosen in **Output**.

> Files passed on the command line (or dropped on the executable on Windows) are added and rendered right away.

**Requirements**: a graphics card (or driver) supporting OpenGL 3.3, and
- **Windows**: Windows 10 (version 1703 or newer) or Windows 11.
- **Linux**: a desktop with X11 or Wayland (XWayland), glibc 2.35 or newer, the OpenGL driver of your graphics card (Mesa works), and `curl` and `tar` with `xz` for the automatic ffmpeg download. On hybrid-GPU laptops, start the program with `DRI_PRIME=1` to use the discrete GPU.
- **macOS**: 10.13 or newer. The archive is signed ad hoc, not notarized: on first launch, right-click `ShaderToMP4.app` and choose *Open* (or run `xattr -dr com.apple.quarantine ShaderToMP4.app`). Settings are written in the folder that contains the `.app`, so keep it in a folder you can write to (not `/Applications`).

## Features

<details>
<summary><b>Buttons and keyboard shortcuts</b></summary>

| Button | Shortcut | Role |
|---|---|---|
| **Add** | `Ctrl+O` | Add shaders to the queue |
| **Start** | `F5` | Render every file of the list that is not already finished |
| **Pause** / **Resume** | `F6` | Suspend the render (paused time is not counted in the speed) |
| **Cancel** | `Esc` | Stop the render; finished parts are kept |
| **PNG image** | | Render a single image at the “Start” time and open it |
| **Preview** and slider | `Ctrl+P` | Show a frame of the shader at a chosen time, without ffmpeg |
| **Remove** | `Del` (in the list) | Remove the selected shaders |
| **Folder** | | Open the folder of the selected shader's video |
| **Profile / Save** | | Load or save all settings in an `.ini` file |
| **About** | `F1` | Version, copyright, license, contact, website |
| **EN / FR** | | Switch the interface language |

`Tab` moves from one setting to the next.

</details>

<details>
<summary><b>All settings</b> (window, <code>config.ini</code> or shader comments)</summary>

Settings are saved in `config.ini` next to the `.exe`, and can be forced **per shader** with a comment (it takes priority over the window; the log says so):

```glsl
// fps=30
// duration=20
// width=3840
// height=2160
void mainImage(out vec4 fragColor, in vec2 fragCoord) { ... }
```

| Setting (window) | Key | Values |
|---|---|---|
| Resolution | `width`, `height` | 16 to 16384 (rounded to an even number) |
| FPS | `fps` | 1 to 240 |
| Duration, Start | `duration`, `start` | seconds (`90`) or `mm:ss` in the window |
| Codec | `codec` | `h264`, `h265` |
| Preset | `preset` | `ultrafast` ... `veryslow` |
| Quality (CRF) | `crf` | 0 (lossless), 12 (nearly perfect), 18 (very good) |
| Bitrate | `bitrate` | kb/s; 0 = constant quality (CRF) |
| Container | `container` | `mp4`, `mov`, `mkv` |
| Encoder | `encoder` | `cpu`, `auto`, `nvenc`, `amf`, `qsv` (falls back to the CPU if unavailable) |
| Smoothing | `ss` | 1 to 4: render at N× the size, then downscale (supersampling) |
| Color | `color` | `bt709`, `bt709full` (full range), `srgb`, `bt601` |
| Blur | `blur`, `shutter` | 1, 4, 8, 16 sub-frames per frame; shutter (0.5 = 180°) |
| Loop (s) | `loop` | shader period: the duration becomes a whole multiple, the last frame comes exactly before the first |
| Audio | `audio` | sound file, aligned with the “Start” time, padded with silence if shorter |
| Fade in / out | `fadein`, `fadeout` | seconds, picture and sound |
| Output | `outdir` | output folder (empty = next to the shader) |
| (config.ini only) | `chunk` | render in parts of N seconds (30 by default, 0 = never) |
| (config.ini only) | `pbo` | depth of the GPU read-back queue (2 to 8, 3 by default) |

</details>

<details>
<summary><b>Resuming after an interruption</b></summary>

Videos longer than `chunk` seconds are rendered in parts, in a `<name>.parts` folder next to the video. If the render is cancelled, crashes or fails, the finished parts stay: run the same render **with the same settings** and it resumes where it stopped, then assembles the parts (stream copy, no re-encoding). If the shader or a setting changed, the old parts are discarded.

</details>

<details>
<summary><b>Command line</b></summary>

| Command | Effect |
|---|---|
| `ShaderToMP4 a.glsl b.glsl` | Add the files and start rendering |
| `ShaderToMP4 --selftest [--update] [--quiet]` | Self-test (see below) |
| `ShaderToMP4 --get-ffmpeg [--quiet]` | Download and install `ffmpeg` without opening the window (Windows and Linux) |
| `ShaderToMP4 --version` | Print version, copyright and license |
| `ShaderToMP4 --help` | Print the options |

</details>

<details>
<summary><b>Languages</b></summary>

All interface texts come from `lang/en.json` and `lang/fr.json`, embedded in the executable. The language follows the system by default, can be switched with the globe button, and is remembered in `window.ini`. To add or fix a language without rebuilding, put a `lang/<code>.json` file next to the executable (keys missing from it fall back to English); the globe button then cycles through every available language.

</details>

## FAQ

**Which Shadertoy shaders are supported?**
Those that only use the *Image* tab and the `iResolution`, `iTime`, `iTimeDelta`, `iFrame`, `iFrameRate`, `iDate` inputs. Paste the code in a `.glsl` file: the `mainImage` function is enough.

**What about textures, buffers, sound or the mouse?**
Not yet: `iChannel0` to `iChannel3` (textures, buffers, multipass), `iMouse` (fixed at 0) and Shadertoy sound are not supported. A shader using them shows a warning and renders black for those inputs.

**Why not a simple screen capture?**
A capture depends on your PC's speed: if the shader is heavy, the video stutters. Here each frame is rendered at its exact time, as slowly as needed.

**My render is slow. What can I do?**
Try hardware encoding (`Encoder: Auto`), a faster preset, or the *Draft* preset to test. The log tells you whether the GPU or the encoder is the bottleneck.

**Where is `ffmpeg`?**
On Windows and Linux it is downloaded on request next to the program. You can also place it there yourself, or keep it in your `PATH` (on macOS the Homebrew folders are searched too).

**Which hardware encoders are supported?**

| System | Encoders (`Encoder` setting) |
|---|---|
| Windows | NVENC (NVIDIA), AMF (AMD), QuickSync (Intel) |
| Linux | NVENC, VAAPI (AMD / Intel), QuickSync |
| macOS | VideoToolbox |

`Auto` tries them in that order with a 0.2 s test encode and falls back to the CPU when none works.

**Does it install anything or write elsewhere on my PC?**
No. The program is portable: settings (`config.ini`, `window.ini`), languages and `ffmpeg` live in its own folder. The program itself does not write to the registry, to your user profile or to any system folder (graphics drivers may keep their own caches).

## Building

### Windows

With the Visual Studio Build Tools (the path of `vcvars64.bat` is written in `scripts\build.bat`; adjust it if needed):

```
.\scripts\build.bat
```

This compiles the resources (manifest, icon, SVG icons, language files, version information) then every file of `src` into `bin\ShaderToMP4.exe`, statically linked (no runtime to install), and copies the license files and `lang/` next to it. CMake works too (`cmake -S . -B build-cmake` then `cmake --build build-cmake --config Release`), and `scripts\package-windows.ps1` makes the portable zip.

### Linux and macOS

You need CMake 3.20+, a C++17 compiler, and on Linux the X11 / Wayland development packages (on Debian and Ubuntu: `sudo apt install cmake ninja-build xorg-dev libgl1-mesa-dev libxkbcommon-dev libwayland-dev wayland-protocols`).

```
./scripts/build.sh          # configures, downloads GLFW, Dear ImGui and NanoSVG (pinned versions) and builds bin/ShaderToMP4
./scripts/test.sh           # self-test
./scripts/package-linux.sh x86_64      # or aarch64: dist/ShaderToMP4-<version>-linux-<arch>.tar.gz
./scripts/package-macos.sh             # dist/ShaderToMP4-<version>-macos-universal.zip (build with -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64")
```

The first configuration needs an internet connection to fetch the three libraries.

<details>
<summary>MinGW-w64 (untested)</summary>

Direct2D's SVG rendering needs recent MinGW headers.

```
g++ -std=c++17 -O2 -finput-charset=UTF-8 -municode -mwindows -static -I src -o bin/ShaderToMP4.exe src/main.cpp src/core/*.cpp src/gui/*.cpp -lopengl32 -lgdi32 -luser32 -lcomctl32 -lcomdlg32 -lshell32 -ladvapi32 -lwininet -ld2d1 -ld3d11 -ldxgi -ldwrite -lshlwapi -luxtheme -ldwmapi -lole32
```

</details>

### Project layout

```
src/        source code: core/ (rendering engine, no UI), platform/ (the only code that calls the operating system: windows/, posix/),
            gui/ (Windows interface, Direct2D), ui_imgui/ (Linux and macOS interface, GLFW + Dear ImGui), main.cpp, main_posix.cpp
assets/     fonts embedded in the Linux / macOS interface (Inter)
cmake/      resource embedding for CMake builds
lang/       interface texts: en.json, fr.json
res/        manifest (DPI, theme), icon, version information, SVG icons (icons/)
scripts/    build.bat / build.sh (build), test.bat / test.sh (self-test), package-*.{ps1,sh} (portable archives), check-repo.ps1 (repository check)
tests/      reference shaders and their .ref images
examples/   example shaders
docs/       banner, screenshots, website
bin/        working folder: the executable (generated), ffmpeg, config.ini
```

### Self-test and repository check

`scripts\test.bat` (`./scripts/test.sh` on Linux and macOS) renders the shaders of the `tests` folder at 64×64 (at t = 1 s) and compares them with their `.ref` reference images (mean difference tolerated: 2/255). The report is in `tests\report.txt` and the exit code is 0 when everything passes. References depend on the GPU and driver: after an intended rendering change or on new hardware, regenerate them with `scripts\test.bat --update`.

`scripts\check-repo.ps1` checks the repository before each commit: license headers, matching language files, no hard-coded interface text, bilingual documentation, portability rules.

### Known limits

- The interface adapts to the screen DPI but has only been checked at 100 %.
- AMD and Intel hardware encoders (AMF, QuickSync, VAAPI) have not been tested on real hardware.
- macOS: the program is built and packaged by the continuous integration, but the hosted macOS machines have no GPU, so the window has not been run on a real Mac. Please report any problem.
- Linux and macOS: the file dialogs are built into the program (no system dialog), and there is no taskbar / dock progress indicator.

## Project information

- **Copyright** © 2026 Patrick JAILLET
- **License**: distributed under the GPL-3.0-or-later license, see [LICENSE](LICENSE) and [COPYING](COPYING). Third-party components: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). `ffmpeg` is a separate program, not included in this repository: it is downloaded on request from [gyan.dev](https://www.gyan.dev/ffmpeg/builds/) (Windows) or [BtbN](https://github.com/BtbN/FFmpeg-Builds) (Linux) and remains under its own license.
- **Contact**: [sandefjord.development@proton.me](mailto:sandefjord.development@proton.me)
- **Website**: <https://ap3demak-rgb.github.io/ShaderToMP4>
