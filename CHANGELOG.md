<!-- SPDX-License-Identifier: GPL-3.0-or-later - Copyright (C) 2026 Patrick JAILLET -->

**English** · [Français](CHANGELOG.fr.md)

# Changelog

Format: one section per version, newest first. The section of a tagged version is copied into the release notes.

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
