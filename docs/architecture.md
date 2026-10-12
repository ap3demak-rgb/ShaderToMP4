<!-- SPDX-License-Identifier: GPL-3.0-or-later - Copyright (C) 2026 Patrick JAILLET -->

[Français](architecture.fr.md) · **English**

# Architecture: one engine, two front-ends

*Decision record for the multi-platform work (Windows, Linux, macOS). Written for contributors.*

## 1. Goal and constraints

ShaderToMP4 must run on **Windows, Linux and macOS**, as a **portable** program (no installer, nothing written outside its folder), distributed under **GPL-3.0-or-later**, with every third-party dependency GPL-compatible and listed in [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md). All interface text comes from `lang/*.json`.

Today the code is split in two layers:

- `src/core/`: the rendering engine (OpenGL 3.3 core, ffmpeg pipe, chunking and resume, settings, i18n, self-test). It is independent of the interface and calls no operating system API: everything goes through `src/platform/`.
- `src/gui/`: the Windows interface (Win32 + Direct2D/DirectWrite, SVG through Direct2D).

## 2. Options considered for the interface

| Option | For | Against |
|---|---|---|
| **Keep Direct2D on every system** | Already done | Windows-only API: impossible on Linux and macOS. |
| **Qt (static)** | Native widgets, SVG built in | Executable of tens of MB, heavy build, LGPL/GPL obligations to track, dark look to rebuild with style sheets. |
| **wxWidgets** | Native look | The look differs on each system, no coherent dark theme or SVG without extra work. |
| **GLFW + Dear ImGui + NanoSVG** | Light, same rendering on every system through OpenGL (already required), fetched at configuration time with pinned versions, licenses MIT / zlib (GPL-compatible), file drop and DPI handled by GLFW | The current look has to be recreated with ImGui styling; no native widgets. |

## 3. Decision

- **Windows keeps the current interface** (Direct2D/DirectWrite, SVG icons, dark theme, OLE drag and drop, taskbar progress). It is the reference experience and stays untouched.
- **Linux and macOS use a new front-end** built on **GLFW** (window, input, OpenGL context, file drop), **Dear ImGui** (widgets, styled to match the Windows look: dark cards, rounded corners, gradient accent) and **NanoSVG** (rasterises the same `res/icons/*.svg` files into textures). One font is embedded in the executable so that text renders identically on both systems.
- **Both front-ends share everything below them**: the engine, the translations, the settings and the presets. Each front-end keeps its own window state (`g` in `src/gui`, `Model` in `src/ui_imgui`) and talks to the engine through `Hooks`.

```
 +--------------------------+     +--------------------------+
 |  Windows front-end       |     |  Linux / macOS front-end |
 |  src/gui (Direct2D)      |     |  src/ui_imgui (GLFW+ImGui)|
 +------------+-------------+     +------------+-------------+
              |                                |
              +---------------+----------------+
                              |
                  +-----------v-----------+
                  |  src/core (engine)    |   render, encoder, settings, formats, passes,
                  |                       |   channels, queue, cli, i18n, ...
                  +-----------+-----------+
                              |
                  +-----------v-----------+
                  |  src/platform         |   only place with OS-specific code
                  |  windows/ posix/      |
                  +-----------------------+
```

## 4. The platform layer (`src/platform/`)

The engine never calls an operating-system API directly. It uses `platform::` functions, implemented once for Windows (`windows/`) and once for Linux and macOS (`posix/`):

| Area | Functions |
|---|---|
| Files and paths | application folder, existence, read/write, create/remove folders, rename/move, free disk space, directory listing, temporary files (all paths are UTF-8 internally) |
| Processes | start a hidden process with pipes (stdin for frames, merged stdout/stderr), wait, terminate |
| Network and hashing | HTTPS download with cancellation (WinInet on Windows, the system `curl` elsewhere), SHA-256 (CryptoAPI on Windows, built-in code elsewhere), archive extraction through the system `tar` |
| OpenGL | create a hidden context, load function pointers, make current on a thread |
| System | open a file, folder or URL with the default application, user language, clock |
| Embedded data | read an embedded resource (language files, icons) |

The engine keeps `std::wstring` for text and paths; the platform layer converts to the native encoding at the boundary (UTF-16 on Windows, UTF-8 on Linux and macOS).

## 5. Threads and OpenGL

- The interface runs on the **main thread** (a requirement of macOS windowing).
- Rendering runs on a **worker thread** with its own hidden OpenGL context created by the platform layer. On Linux and macOS the hidden window is created on the main thread at start-up and made current on the worker thread.
- Workers talk to the interface only through the shared application layer (messages / atomics), as today on Windows.

## 6. ffmpeg

`ffmpeg` is never bundled. Per system, the program first looks for a copy next to itself, then for a suitable one in the `PATH`, and otherwise offers to download a static build (SHA-256 checked) into its own folder:

| System | Source |
|---|---|
| Windows | gyan.dev "essentials" build (current behaviour) |
| Linux (x86-64, ARM64) | static GPL build from BtbN (SHA-256 taken from the checksum file published with it) |
| macOS (Intel, Apple Silicon) | no automatic download: no source publishes a verifiable SHA-256 for macOS builds. The program looks next to the app, in the `PATH` and in the Homebrew folders. |

Hardware encoders follow the system: NVENC (Windows, Linux), AMF and QuickSync (Windows), VAAPI (Linux), VideoToolbox (macOS), with the same probe and CPU fallback.

## 7. Build, tests and distribution

- **CMake** builds Linux and macOS (and can build Windows too); `scripts/build.bat` stays the simplest Windows route.
- **GitHub Actions** builds and tests on Windows, Linux and macOS: compile, self-test (Mesa software rendering on Linux), `scripts/check-repo.ps1`, portable archives, release publication from a tag.
- **Portable archives only**: `win64.zip`, `linux-x86_64.tar.gz`, `linux-aarch64.tar.gz`, `macos-universal.zip` (a `.app` inside the archive, no `.dmg` or `.pkg`), with SHA-256 sums.

## 8. Consequences and risks

- Two interfaces to maintain: the shared application layer keeps the logic common so that only drawing and event handling differ.
- The Linux/macOS look is an ImGui re-creation of the Windows one; small visual differences are expected and acceptable.
- macOS caps OpenGL at 4.1 core (3.3 core is enough) and deprecates it; a Metal path is out of scope.
- Linux and macOS cannot be run on the maintainer's machine: continuous integration is the test bed. On Linux (x86-64 and ARM64) it builds, runs the self-test with Mesa software rendering under a virtual display and produces window screenshots. Hosted macOS machines have no GPU (no OpenGL 3.3 pixel format), so macOS is only built, packaged and smoke-tested there (`--version`); the macOS window has not been run on real hardware.
- VAAPI and VideoToolbox encoding follow the documented ffmpeg options but have not been tested on real hardware; the start-up probe falls back to the CPU encoder when they fail.
