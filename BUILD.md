<!-- SPDX-License-Identifier: GPL-3.0-or-later - Copyright (C) 2026 Patrick JAILLET -->

**English** · [Français](BUILD.fr.md)

# Building, testing and publishing ShaderToMP4

This file is for people who build the program from its sources. The free edition (GPL-3.0-or-later) in this repository is the only one described here. Linux and macOS are frozen since 2026-10-22: their code is still in the repository but is neither built nor tested any more (see the end of this file).

## What you need

| Tool | Used for |
|---|---|
| Windows 10 or 11, 64-bit, with a graphics driver that offers **OpenGL 3.3** | running the program and its self-test |
| Visual Studio Build Tools (C++ workload, Windows SDK) | compiling (`cl`, `rc`, `link`) |
| PowerShell 5.1 or later | the scripts of `scripts\` |
| `ffmpeg.exe` | the format tests and every render; `ShaderToMP4.exe --get-ffmpeg` downloads it next to the program (SHA-256 checked) |
| CMake 3.20 or later | optional, an alternative way to build |
| NSIS 3 | only to build the Windows installer (`scripts\package-installer.ps1`) |
| Git | the hooks and the version number checks |

The path of `vcvars64.bat` is written at the top of `scripts\build.bat`, `build-asan.bat` and `build-coverage.bat` (Visual Studio Build Tools 18 in its default folder). Edit it if yours is elsewhere, or set the `VCVARSALL` environment variable to the full path of `vcvars64.bat`.

## Build

```
.\scripts\build.bat
```

The script compiles the resources (`res\app.rc`: manifest, icon, SVG icons, version information) and every source file of `src\main.cpp`, `src\core`, `src\platform` and `src\gui`, with `cl /O2 /MT` (the C++ runtime is linked statically: nothing to install), then links `bin\ShaderToMP4.exe` with `/Brepro` (no time stamp, so two builds of the same sources give the same bytes). It also copies `COPYING`, `LICENSE`, the third-party notices and `lang\` into `bin\`. `build\` holds the object files.

With CMake:

```
cmake -S . -B build-cmake -A x64
cmake --build build-cmake --config Release
```

The program is written to `bin\` too. The program is a window application: log through the interface, never with `printf`.

## Test

```
.\scripts\test.bat            # same as: bin\ShaderToMP4.exe --selftest --quiet
.\scripts\test.bat --update   # record new reference images (see below)
```

The executable is a window application, so a bare call from PowerShell returns at once. Wait for it and read the exit code:

```
$p = Start-Process bin\ShaderToMP4.exe -ArgumentList '--selftest','--quiet' -Wait -PassThru -NoNewWindow
$p.ExitCode      # 0 = everything passed
```

The result is in `tests\report.txt` (lines starting with `PASS`, `FAIL`, `DRIFT` or `UPDATED`). The self-test:

- renders the shaders of `tests\*.glsl` at 64×64 (at t = 1 s) and compares them with reference images; a file whose name has a second dot (`name.bufferA.glsl`) is a pass of a test, not a test;
- renders every output format and reads it back with `ffmpeg -i` (without ffmpeg these cases print `SKIP`: run `--get-ffmpeg` first);
- renders the sixty example shaders of `examples\gallery`, checks the readers with damaged inputs (fuzzing), the error explanations, the update check, the local crash report, the licence checks and the other engine features.

**Reference images per driver.** `tests\ref\<driver>\<name>.ref` is looked for first (`nvidia`, `amd`, `intel`, `llvmpipe`, `warp`...; the name comes from `GL_RENDERER`), then `tests\<name>.ref`. `tests\tolerances.txt` gives each case its tolerance with the difference measured between the two sets kept in the repository (GeForce with the NVIDIA driver, and Mesa llvmpipe). A difference above half of the tolerance prints `DRIFT`. To record the set of your own graphics card, run `--selftest --update` on that machine: it writes only into `tests\ref\<your driver>\`, never over the others. Never change an existing `.ref` to make a failing test pass.

A few cases depend on timing (the cost map, the real-time preview): on a machine that is busy with something else they may fail once; run the self-test again before looking for a bug.

### Software OpenGL (no graphics card)

A machine without OpenGL 3.3 (a continuous-integration runner, a virtual machine) can use Mesa's llvmpipe: put `opengl32.dll` and `libgallium_wgl.dll` of a Mesa build for Windows (for example the `mesa3d-<version>-release-msvc.7z` archive of the `mesa-dist-win` project) next to the executable and set `GALLIUM_DRIVER=llvmpipe`. The `tests\ref\llvmpipe` set is the one used then. Mesa is only for tests: never ship it.

### Fuzzing

```
bin\ShaderToMP4.exe --fuzz=all --fuzz-count=3000 --fuzz-seed=7
bin\ShaderToMP4.exe --fuzz=json           # one target
```

Every reader of a file or a text written in the program (JSON, `.montage`, `.ini`, `.cube`, EXR, floating-point TIFF, the `.beats.json` sheet, `jobs\*.ini`, `queue.txt`, expressions, time curves...) is fed damaged inputs; the exit code is 0 when none crashes, loops for more than 8 seconds or throws (97 and 98 otherwise). The input being tried is always in `fuzz_tmp\fuzz_last_<target>.*`: after a crash it is the faulty case. Copy it to `tests\fuzz\<target>\` so it is replayed from then on. A new reader must get its own target (`core\fuzz.cpp`, `registerFuzzTarget`).

### Memory errors (AddressSanitizer)

```
.\scripts\build-asan.bat      # bin-asan\ShaderToMP4.exe, no optimisation, 16 MB stack
.\scripts\test-asan.bat       # the complete self-test with it: no alert expected
```

It needs the "MSVC AddressSanitizer" component of Visual Studio. A few cases that rely on short delays are skipped under the sanitizer (everything is several times slower), and so is the deliberate crash.

### Coverage

`scripts\build-coverage.bat` builds `bin-cov\` with clang-cl (LLVM, `C:\Program Files\LLVM`; set the `LLVM` variable if it is elsewhere) and source-based coverage; `scripts\coverage.ps1` then runs the complete self-test on it, merges the profiles of every process it starts and writes `coverage\report.txt` (per file) plus the totals of the engine, the windows and the rest. The engine total must stay at or above 80 % of the lines (`-Min`); the windows are not driven by the self-test and show 0 %.

## Repository checks

```
git config core.hooksPath .githooks     # once: runs the checks before every commit
.\scripts\check-repo.ps1                # by hand; it must print "Depot conforme."
```

It checks the license headers (`SPDX-License-Identifier` and the copyright line in every new source file), the matching language files (`lang\*.json`: same keys in the nine languages, every key used, no interface text written in the code), the bilingual documentation (every `.md` exists in English and French), the portability rules (no registry, no user folders), the colour contrast of the palette and the anonymity rules. Interface texts go through `TX("key")` / `TXF("key", ...)`; add each key to all nine `lang\*.json`.

## Other checks

| Script | What it does |
|---|---|
| `scripts\verify-reproducible.ps1` | builds twice with CMake in the same folder and compares the SHA-256 of the two executables |
| `scripts\audit-accessibility.ps1` | opens the main window, About, Help and Examples and checks, through UI Automation, that every control has a name and that Tab reaches all of them |
| `scripts\perf-journal.ps1` | benchmark, time to the main window, memory of a reference render; `-Previous <file>` compares with another journal (a regression of more than 15 % fails) |
| `scripts\soak-test.ps1` | one process renders chunked videos in parallel for `-Minutes` minutes while handles and memory are sampled |
| `scripts\make-sbom.ps1` | software bill of materials (SPDX 2.3) from `scripts\components.json` |
| `scripts\check-vulnerabilities.ps1` | asks the NVD for the known vulnerabilities of the shipped third-party components (network) |
| `scripts\test-queue.ps1` | drives the real window: priority, hold, failure, cancellation, live reordering, mouse drag, `Alt+Up`, project file, `keys.ini`, command palette, redo from the history (do not touch the mouse or keyboard; about two minutes) |
| `scripts\verify-portable.ps1`, `verify-installer.ps1` | the program creates no file and no registry key outside its own folder during a real session; the installer is tried silently (install, update, refusals, uninstall, traces left) |

Keep `scripts\components.json` and `THIRD_PARTY_NOTICES.md` (and `.fr.md`) in step when a component is added.

## Package

```
.\scripts\package-windows.ps1         # dist\ShaderToMP4-<version>-win64.zip (portable archive)
.\scripts\package-installer.ps1       # dist\ShaderToMP4-<version>-win64-setup.exe (NSIS, ffmpeg included)
.\scripts\package-manifests.ps1       # dist\manifests: winget and Scoop manifests of the version
```

Run them after a build. The installer embeds a pinned ffmpeg build whose version and SHA-256 are written in `package-installer.ps1`; the script refuses anything else, and refuses a non-GPL or "nonfree" build. The portable archive does not contain ffmpeg: the program downloads it on request.

## Publish a version

1. Set the number in `src\app_info.h` (`APP_VER_MAJOR`, `APP_VER_MINOR`, `APP_VER_PATCH`, `APP_VER_STR`).
2. In `CHANGELOG.md` and `CHANGELOG.fr.md` rename "Unreleased" to `## <version> — <date>`; update the version badge of the READMEs and of `docs\index.html`.
3. Run the self-test, the AddressSanitizer self-test and `scripts\check-repo.ps1`; commit and push; wait for the continuous integration to be green.
4. `git tag -a v<version> -m "ShaderToMP4 <version>" && git push origin v<version>`.

The continuous integration (`.github\workflows\build.yml`, Windows only) builds, runs the self-test (with Mesa llvmpipe), the format tests, the command line, the benchmark, the AddressSanitizer self-test, the packages, the installer checks, the software bill of materials, the vulnerability report, the manifests, the performance journal and the reproducible-build check. On a tag it publishes the release with `SHA256SUMS.txt`: it refuses a tag that differs from `APP_VER_STR`, a changelog without a section for the version, and (on a tag) a performance regression of more than 15 % against the previous release.

## Frozen platforms

The Linux and macOS front-end (`src\ui_imgui`, `src\platform\posix`, `scripts\build.sh`, `test.sh`, `package-linux.sh`, `package-macos.sh`, `CMakeLists.txt` outside `WIN32`) is kept as it was in version 2.1.1, the last one published with Linux and macOS archives. It needs CMake 3.20+ and a C++17 compiler (GLFW, Dear ImGui and NanoSVG are fetched at pinned versions). No fix, no test and no continuous integration are planned for it.

## Problems

- **"OpenGL 3.3 is required"**: update the graphics driver, or use Mesa llvmpipe as above. `bin\ShaderToMP4.exe --diagnostic` lists what was found.
- **`vcvars64.bat` not found**: adjust the path at the top of the build scripts or set `VCVARSALL`.
- **The self-test prints `SKIP` for the formats**: run `bin\ShaderToMP4.exe --get-ffmpeg` (or put `ffmpeg.exe` next to the program).
- **A window that does not appear**: the position is stored in `bin\window.ini`; delete the file to reset it.
