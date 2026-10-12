<!-- SPDX-License-Identifier: GPL-3.0-or-later - Copyright (C) 2026 Patrick JAILLET -->

**English** · [Français](CHANGELOG.fr.md)

# Changelog

Format: one section per version, newest first. The section of a tagged version is copied into the release notes.

## 1.8.0 — 2026-10-12

### Added

- Animated `iMouse`: keyframe trajectory `// mouse=0:0.2,0.2 ; 5:0.8,0.6,down` (linear interpolation, the fixed form `// mouse=x,y[,down]` still works); `iMouse.zw` follows Shadertoy (last click position, `w` > 0 only on the click frame). Applies to renders, previews and all passes.
- Animated `iDate`: `// date=2026-03-14T15:09:26+rate=60` sets how many seconds of date pass per second of video (default 1, 0 freezes it); the day, month and year now roll over at midnight.
- Simulated keyboard channel: `// iChannel0=keyboard` gives Shadertoy's 256×3 key texture (down, pressed, toggle), with keys pressed at given times by `// key=space:2-4.5 ; left:1-3` (nothing reads the real keyboard). Keyboard inputs of imported Shadertoy shaders now become this channel.
- Buffers with their own size: `// bufferA.scale=0.5` or `// bufferA.size=512x512` (also for B, C, D); the pass sees it in `iResolution`, the readers in `iChannelResolution`.
- `mainVR` shaders (VR) are rendered as 360° equirectangular videos, mono or stereo top/bottom (`// vr=mono|stereo`, `vripd`), with the spherical video metadata added to MP4 and MOV files so that YouTube and VR players treat them as 360°.
- Compatibility report: lists the channels that are set but never read and the passes nothing reads (they run for nothing).
- Shadertoy volume files: `// iChannel0=volume:file.bin` reads the 3D volume format (8-bit or float, 1 to 4 channels).
- `warmup=N` (shader comment or config.ini): simulates N frames before the first one so feedback buffers settle; `iFrame` starts at N. Also applies to chunked renders, resume and the preview.
- `iChannelTime` is now the render time for audio and video channels (it was always 0).

### Changed

- Documentation: the README (English and French) FAQ and the Shadertoy compatibility table now describe what is supported (channels, passes, Sound pass, JSON import).
- Compatibility report: the cubemap and 3D texture messages now say that no channel is set, instead of “not supported yet”.

## 1.7.0 — 2026-10-12

### Added

- Sound pass (`name.sound.glsl`, `mainSound`): rendered by the GPU into a temporary WAV that becomes the video's audio track; also imported from Shadertoy JSON exports.

## 1.6.1 — 2026-10-11

### Changed

- German, Spanish, Italian and Portuguese: all interface and command-line texts translated (they fell back to English for recent features).

## 1.6.0 — 2026-10-11

### Added

- Shadertoy channels: cubemaps (`cube:`), 3D textures (`volume:`), audio spectrum and waveform (`audio`), video (`video:`).
- Cubemap A pass (`name.cubemapA.glsl`, channel `cubeA`).
- Offline import of a Shadertoy `.json` export (UI, command line); no account or API key.

## 1.5.1 — 2026-10-11

### Changed

- Accessibility (Windows): fields, drop-down lists and sliders are named by their label for UI Automation, so screen readers announce “Duration (s)” instead of the value.
- Screenshots of the README refreshed (Windows and Linux, English and French).

## 1.5.0 — 2026-10-11

### Added

- Variants: `// variant=720p:width=1280|height=720` (or `--variant=`) renders the same shader again with other settings in one go.
- macOS: the render progress is shown in the Dock icon.
- Linux: `gpu=high|low` (config.ini or `--gpu=`) picks the discrete or integrated GPU on two-GPU PCs.
- Real-time preview: a Play button plays the shader at preview size, loops, follows the slider and reloads when the shader file changes on disk.
- Windows: `--dpi=N` forces the interface scale (used to check the layout at 125 % to 200 %).

## 1.4.0 — 2026-10-11

### Added
- Output formats: VP9 and AV1 (SVT-AV1, libaom as a fallback; hardware AV1 tried on NVENC, AMF, QuickSync and VAAPI), WebM with Opus sound, ProRes (Proxy to 4444) and DNxHR, animated GIF (optimised palette) and WebP, numbered PNG / TIFF / EXR image sequences.
- 10-bit output: 16-bit rendering, H.265 Main10, AV1, VP9, ProRes / DNxHR, 16-bit PNG / TIFF.
- Estimated file size under the settings, with a disk space warning; codecs and containers missing from the installed ffmpeg are marked in the lists.
- File name template (`name`) and rule for an existing file (`exists`: overwrite, number, skip); container metadata (title, comment) and `+faststart`.
- “More” presets menu (X 720p30, light GIF, ProRes for editing, WebM VP9) and your own presets in a `presets` folder.
- Shadertoy compatibility: `iChannel0-3` textures (PNG, JPG, HDR, EXR, generated `noise` and `grid`, filter / wrap / flip options), fixed `iMouse` and `iDate`, Buffer A–D and Common passes with persistent float buffers (also when a render is resumed or previewed), `#version` / `#extension` ignored, `texture2D` and other GLSL ES names accepted, and a compatibility report in the log before each render.
- Command line: `--render` with every setting as `--key=value`, `--profile=`, `--png`, JSON progress (`--progress=json`), documented exit codes, same on Windows, Linux and macOS.
- The settings card shows “Set by the shader: fps=30, …” for the selected shader (the `// key=value` comments take priority over the window).
- GLSL compile errors are written `file:line: message`; double-clicking such a line in the log opens the file at that line.
- The queue survives closing the program: shaders still to render are restored from `queue.txt` at the next start.
- End of batch: the taskbar / Dock button flashes, and an optional action (`endaction`: show the folder, or sleep after 15 s) can be set in `config.ini` (also with `--render`).
- Interface languages: German, Spanish, Italian and Portuguese (`lang/*.json` shipped next to the program; not yet reviewed by native speakers), chosen from the system language when available.
- Accessibility: discreet and accent text now reach a 4.5:1 contrast (WCAG AA); the repository check verifies it and that both interfaces share the same palette.
- `--watch=folder`: renders every shader dropped in a folder (explicit, off by default).
- `--diagnostic` (system, graphics card, OpenGL, ffmpeg and encoders, no personal data) and `--benchmark` (frames per second by resolution and encoder).

### Changed
- The self-test also renders every output format and re-reads each file with ffmpeg; the continuous integration now really runs it on Windows (software OpenGL) as well as on Linux.

## 1.3.0 — 2026-10-10

- Linux (x86-64, ARM64) and macOS (universal) versions with a GLFW + Dear ImGui interface of the same look as the Windows one; platform layer; portable archives for the three systems built by the continuous integration; ffmpeg download with SHA-256 check on Linux; VAAPI and VideoToolbox encoders; `--render` without a window.

## 1.2.0 — 2026-10-10

- License GPL-3.0-or-later, bilingual interface (English / French) and documentation, About window, official website, repository checks.

## Before 1.2.0

- First public version, then the dark interface (Direct2D, SVG icons): cards, animated buttons, preview, shortcuts, OLE drag and drop, taskbar progress.
