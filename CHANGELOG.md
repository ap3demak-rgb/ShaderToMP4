<!-- SPDX-License-Identifier: GPL-3.0-or-later - Copyright (C) 2026 Patrick JAILLET -->

**English** · [Français](CHANGELOG.fr.md)

# Changelog

Format: one section per version, newest first. The section of a tagged version is copied into the release notes.

## Unreleased

### Added

- Effect mode: `source=` (video, image, image sequence or folder) becomes `iChannel0`; the picture size, rate and duration follow it (`sourcematch`), `iTime` / `iFrame` are those of the source, its sound is kept (`sourceaudio`), its subtitles and chapters are copied (MP4, MOV, MKV). Values written in the shader or on the command line win.
- Effect mode: range of the source (`start`, `duration`), frame skipping or repeating without interpolation (`fps`), `reverse=1` (picture and sound), still images held for the whole render, image sequences (`sourcefps`).
- Folder of sources: one render per file, finished files skipped when repeated, a failure does not stop the others, `_batch.json` report, `{source}` token for the file name.
- Transitions: `source2=` and `iProgress` (0 to 1) with `transitionduration` and `transitionoffset`; the sounds are cross-faded; 25 shaders in the new `transitions/` folder.
- Chroma key, luma key and difference key example shaders with an alpha output (`examples/effects/`).
- “Source (effect mode)” page of the Output window (Windows) and section of the settings card (Linux and macOS); seven new self-test cases; the packages and the installer carry `transitions/` and `examples/effects/`.
- Multilayer OpenEXR written by the program itself, with no library: HDR colour in floating point, output passes declared in the shader (`// aov depth=bufferB.r`, `normal`, `id`, `alpha`, `motion`), ZIP / ZIPS / no compression, half or 32-bit float, header with the shader, author, license and alpha mode; reader of the program for the self-test; `--exr-info`.
- Vector motion blur of the EXR output (`vectorblur`), along the estimated or shader-computed motion pass.
- Estimated motion vectors between consecutive frames (block matching, labelled as an estimate); depth bounds `depthrange` declared in the file; 16-bit depth map `--depthmap`.
- Straight or premultiplied alpha (`alphamode`) for EXR, 16-bit PNG, TIFF and DPX; floating-point 32-bit TIFF sequences (`tifffloat=1`); DPX sequences (`container=dpx`, 10 bits, BT.709 and frame rate in the header); “Compositing (EXR)” page of the Output window and section of the settings card; eight new self-test cases.

### Fixed

- Sound normalization (`loudnorm`): the volume of the music no longer rises and falls during the video. The one-pass `loudnorm` filter of ffmpeg changed its gain over time; the sound is now measured first and one constant gain is applied (single renders and montages). A peak limiter is used only if the peaks would exceed -1.5 dBTP. New self-test case.

## 2.1.0 — 2026-10-16

### Added

- Windows installer (`ShaderToMP4-<version>-win64-setup.exe`, NSIS), in addition to the portable archive, which stays the reference. Per-user, no administrator rights (Program Files refused), `ffmpeg.exe` 9.0.2 (GPL build, pinned and SHA-256 checked, never nonfree) included, interface in the nine languages, Start menu and optional desktop shortcut, optional “Open with” for `.glsl` and `.montage`, entry in Apps & features, update in place that keeps the settings, refusal of an older version, an `ffmpeg.exe` of the user never replaced, uninstall that keeps the settings unless asked (`/DELETE_SETTINGS`). `scripts/package-installer.ps1` builds it, `scripts/verify-installer.ps1` tries it. Not code-signed.

## 2.0.0 — 2026-10-16

### Added

- Seamless loops without touching the shader: `loopmode=crossfade` (the start of the video blends with the shader one duration later, `loopfade`) and `loopmode=pingpong` (the time goes up then comes back); a seam check after the render (last frame against first, in the log and the render report).
- Time curve `timemap=0:0;4:1;8:5` (smooth, with freezes and reverse) and constant `speed`: real slow motion, every frame computed at its exact shader time, motion blur following the speed; the sound is not changed.
- Loop period search: **Find the loop** button and `--find-loop[=seconds]` (no render), the period fills the **Loop** field.
- Smart thumbnail (Images menu, `--png=best`): the three richest frames as `_thumb.png`, `_thumb2.png`, `_thumb3.png`.
- Averaged still image: `samples=N` (`--png --samples=N`) averages N independent samples of one instant in floating point; a cancel keeps the samples already computed.
- “Time and loop” section of the Output window (Windows) and of the settings card (Linux and macOS); seven new self-test cases.
- Quality check before publication (`--check`, Images menu, `precheck=1`): flashes and saturated red flashes (Harding-type indicator), invalid values with the suspect lines and a map of the pixels, black / white / burnt frames, flicker, aliasing, precision at large times; report next to the video, exit code 6.
- Quality check: regular stripe patterns, and a half-precision simulation (`iTime` and `iResolution` rounded, picture compared with the exact one); the estimate of the Images menu covers the whole queue and gives its total.
- `--compare a b` (mean and maximum difference, PSNR, difference image, comparison video; exit code 7), reference frames of one's own shaders (`--golden=record|verify`), comparison with the version before editing in the editor with a curtain to drag in the preview.
- Render time estimate (Images menu, `--dry-run`, whole queue): measured on a few seconds of real rendering with its bounds, and a slowness alert that suggests lowering the supersampling, the blur sub-frames or the size.
- Posters by tiles with Buffer / Cubemap passes (buffers computed once at full size, up to 8192 × 8192 pixels); `stillformat=tiff` (8 / 16 bits, also by tiles); PNG still images tagged sRGB; the cost map also shows in the preview; GPU time of each pass (Buffer A to D, Cubemap A) in the report and the running averages in the status bar; `vr=fulldome` for a plain `mainImage` shader (domemaster).
- Buffer / Cubemap passes with a time that goes back (ping-pong, falling time curve): the state is replayed from the start of the range for each frame (cost limit of 400000 simulation steps, announced in the log); `audiofollow=1` makes the sound follow a constant speed (`atempo`, pitch kept); the Output window gets a third page, “Time and loop”, with a live graph of the time curve; the three smart thumbnails open together.
- Poster by tiles: a still image larger than 8192 pixels (or than the GPU allows, up to 65536) is computed tile by tile with the whole `iResolution` and a shifted `fragCoord`, identical to a single render; PNG bands kept to resume, supersampling, 16 bits (`depth=10`), `samples`, `printdpi=300`; `tile=` to choose the tile size.
- GPU cost map (`--heatmap[=16x9]`, Images menu): the image tinted from blue to red by the GPU time of each zone, with the figures in the log; average GPU time per frame of the image, the buffers and the post-processing in the log and in the render report (`gpu_ms`).
- VR projections for `mainVR`: `vr=sbs` (side by side), `vr=anaglyph` (red-cyan), `vr=fulldome` (180° fisheye square), spherical metadata for the stereo modes; `--cubefaces[=size]` writes the six upright cubemap faces.
- Publication targets (`target=tiktok`, `youtube-shorts`, `instagram-reels`, `discord-8mb`… and your own in `presets/targets.ini`): size, cadence, codec and weight of the platform, with a warning when the duration or the shader goes beyond; target file size (`targetsize=8MB`, two passes, corrected until the file fits); centred crop (`crop=9:16`).
- Master cache (`mastercache=1`): one lossless render, every format derived from it by ffmpeg without the GPU, bounded by `cachemax` GB, emptied by `--cache-purge` or a button.
- Reproducible video (`embed=1`, `--reproduce`), license and credit read in the header of the shader (`autocredit=1`, warning for non-commercial licenses), integrity check (`--verify`, Images menu) against the SHA-256 written in the render report, animated GIF / WebP preview (`animpreview`).
- Standalone HTML page (WebGL 2) from a shader and its passes (`--export-html`, Images menu), checked against the render of the program in a headless browser.
- “Publication” page of the Output window (Windows) and section of the settings card (Linux and macOS); eight new self-test cases.
- Sound and rhythm: the sound file is analysed (tempo, beats, bars, attacks per band, smoothed energy, sections) and kept in `<sound>.beats.json`; any shader reads `iBeat`, `iBeatCount`, `iBPM`, `iBass`, `iMid`, `iTreble`, `iEnergy` and `iOnset` without declaring them, sampled at the exact time of each frame; parameters follow them by an expression (`// uniform float k = iBass*0.5+0.2 attack=0.02 release=0.3`, `--param=`, with `threshold`); `bpm` forces the tempo, `beatsperbar` sets the bars; `--beats[=list|clear]`.
- Markers (`markers=chapters|file|all`): the sections as chapters of the video, every beat, bar and section in `.markers.txt` and `.markers.csv`; montage whose cuts fall on the beats, bars or sections of the music (`beatsync`).
- Beat editor (**Sound and rhythm…** on the Publication page of the Output window, and in the settings card on Linux and macOS): the waveform with the detected beats, bars and sections, a click removes or adds a beat; seven new self-test cases (click track at 120 BPM).
- Parts rendered at the same time (`parallel=N`): N OpenGL contexts render the parts of a video together, same frames as the render one part after the other; a context that fails is removed with a message and its work goes to the others; a slow part is also given to a free context (the first to finish wins); serial with a message for shaders with history passes and on Linux and macOS. `--benchmark --parallel` measures the gain (small on the test PC).
- Render farm without a server (`farm=folder`, `--farm=`): several computers render the parts of one video on a shared folder, each part reserved by an atomic lock file, locks of switched-off computers taken over after `farmtimeout`, finished parts never redone, the first computer that sees every part assembles; another job, version, ffmpeg or encoder is refused with a precise message; `--farm-status folder` and `farm-report.json`. No network connection is opened.

## 1.9.0 — 2026-10-12

### Added

- Transparency (`alpha=1`): the shader's alpha channel in ProRes 4444, VP9 WebM, WebP and PNG / TIFF / EXR sequences (straight alpha, exact with supersampling and motion blur, checkerboard in the previews, other combinations refused before the render).
- HDR output (`hdr=pq` for HDR10, `hdr=hlg`): BT.2020, 10 bits, floating-point conversion, declared MaxCLL / MaxFALL / mastering display (`hdrpeak`, `hdrfall`, `hdrwhite`), in H.265, AV1 and VP9.
- Output size and cadence: `scale` with a choice of filter (`scalefilter`), frame interpolation `interp` (render at 30, write at 60), sound normalization `loudnorm` in LUFS.
- Text on the video: `title`, `credit`, `titledur` with ffmpeg's drawtext; the log of a Shadertoy import gives the credit line to add.
- Montage: a `name.montage` file chains several shaders with cut, fade, crossfade or wipe transitions (clips rendered in resumable parts, then joined by ffmpeg).
- Command line: `--sheet=4x3` (contact sheet) and `--thumb[=seconds]` (cover thumbnail).
- Editor for the selected shader and its passes (Buffer A–D, Common, Cubemap A, Sound, Post): tabs, one-click creation of a missing pass, line numbers, GLSL colouring on Windows, compiler errors marked from the log, `Ctrl+S` writes the file and reloads the preview.
- Settings for one shader only (“This shader only”, kept in `jobs/`, optionally written into the shader as `// key=value` comments); fields greyed out and showing the shader's value when a `// key=value` comment imposes the setting.
- Queue: move up / down / to the top (`Alt+Up` / `Alt+Down`), render only the selection, mark as pending, right-click menu on both interfaces.
- Dropping an image, a sound or a video on the window writes `// iChannelN=…` (or the soundtrack) in the selected shader.
- Preview: frame number and real frame rate while playing; PNG image at the preview time.
- “Advanced” page of the Output window: file name template, existing file rule, metadata, end-of-batch action, parts, GPU read queue, blur shutter.
- Render report (`report=json|text`): time, speed, bottleneck, size and warnings next to each video.
- Command line: `--watch` looks in sub-folders with a `profile.ini` per folder, `--watch-move` (done/ and failed/), `--log=file`; `--benchmark --compare=previous.json --max-drop=N` (exit code 5); continuous integration compares the benchmark with the previous run.
- Linux: headless rendering through EGL when there is no display server (render farm, container, CI), with a CI step that runs the self-test that way.
- Accessibility checked with NVDA: names, roles and values are now also given through MSAA (icon-only buttons, the width and FPS fields, the shaders list, sliders with their value, the progress bar); the editor switches tabs with `Ctrl+Tab` and `Ctrl+1..9`; windows moved to a monitor of another scale are laid out again and kept inside the screen.
- Dutch, Japanese and Simplified Chinese interfaces (`lang/nl.json`, `ja.json`, `zh.json`); on Linux and macOS a system CJK font is loaded for Japanese and Chinese.
- Batches: number shaders 1 to 4 and render one batch; settings of one shader now cover the Output window and the variants; GLSL colouring in the Linux / macOS editor; accessible names for the icon buttons and the new fields; the editor follows a change of screen scale.
- “Output” window (Windows, header button) and matching section of the settings card (Linux and macOS) for transparency, HDR, output size and filter, interpolation, loudness, title and credit; the “PNG image” button becomes “Images”: PNG image, cover thumbnail or contact sheet (3×2, 4×3, 5×4, 6×5).

- Animated parameters: `// uniform float speed = 1.0 [0..5]` (types float, int, bool, vec2, vec3, vec4, color) declares an adjustable uniform available in every pass; values saved per shader in `params/`, keyframes `// key speed=0:1.0 ; 4:3.0` (linear, smooth or constant), `--param=name=value` on the command line, and parameters that follow the sound (`audio.level`, `audio.bass`, `audio.mid`, `audio.treble`).
- “Parameters and post-processing” window on Windows (header button) and the same controls at the bottom of the settings card on Linux and macOS; the live preview picks the values up at once.
- Post-processing in floating point, before the 8 / 10-bit conversion (so with supersampling, motion blur and 10-bit output): a `name.post.glsl` pass that receives the image in `iChannel0`, eight effects (`sharpen`, `chroma`, `bloom`, `contrast`, `saturation`, `hue`, `vignette`, `grain`, `// post=bloom:0.4,grain`) and a color LUT in `.cube` format, 1D or 3D with tetrahedral interpolation (`// lut=grade.cube`).

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
