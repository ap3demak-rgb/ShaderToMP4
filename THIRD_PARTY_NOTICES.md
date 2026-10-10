<!-- SPDX-License-Identifier: GPL-3.0-or-later - Copyright (C) 2026 Patrick JAILLET -->

[Français](THIRD_PARTY_NOTICES.fr.md) · **English**

# Third-party notices

ShaderToMP4 is distributed under the **GNU General Public License v3.0 or later** (`GPL-3.0-or-later`, see [COPYING](COPYING)). This document lists every third-party component the program uses, with its license and why it is compatible with the GPL-3.0. **No component may be added to the project without an entry here.**

Last review: ShaderToMP4 1.2.0.

## 1. Code compiled into the program

None. The JSON reader (translations), the Direct2D drawing layer, the process, HTTP and file helpers are original code of the project (`src/`). Only the C++ runtime and the Windows SDK are used at build time (see section 3).

## 2. Components used at run time

| Component | Role | How it is used | License | GPL-3.0 compatibility |
|---|---|---|---|---|
| **FFmpeg** ([ffmpeg.org](https://ffmpeg.org)), "release essentials" build by Gyan Doshi ([gyan.dev](https://www.gyan.dev/ffmpeg/builds/)), with libx264 and libx265 | Video / audio encoding | **Not included** in this repository nor in the release archives. The program downloads it on request (SHA-256 checked) into its own folder and runs it as a **separate process** (pipes). No linking. | GPL v3 for this build (it enables the GPL libraries libx264 and libx265; FFmpeg itself is LGPL 2.1+ / GPL 2+). Full license text and sources: ffmpeg.org and gyan.dev. | Compatible: GPL v3, no code combined. |
| **Windows system components**: Direct2D, DirectWrite, Direct3D 11, DXGI, OpenGL (graphics driver), WinInet, CryptoAPI, Shell, Common Controls, RichEdit, OLE drag and drop, taskbar progress (ITaskbarList3) | Interface, rendering, download, hashing | Provided by Windows and the graphics driver; **not redistributed**. | Microsoft Windows license / graphics driver license | Covered by the GPL-3.0 "System Libraries" definition: they are part of the operating system the program runs on. |
| **Fonts**: Segoe UI (interface), Cascadia Mono (log) | Text | Provided by Windows; **not redistributed**, not embedded. Cascadia Mono falls back to a system font when absent. | Microsoft font licenses / SIL OFL 1.1 (Cascadia) | Not distributed with the program, so no license obligation is transferred. |

## 3. Tools used to build the program (not distributed)

| Tool | License note |
|---|---|
| Microsoft Visual C++ compiler, linker and resource compiler (Visual Studio Build Tools), Windows SDK headers and import libraries | The C/C++ runtime is **statically linked** (`/MT`): Microsoft's runtime library license allows redistribution as part of an application, and the GPL-3.0 treats the compiler's runtime libraries as System Libraries. |
| Windows PowerShell, `tar.exe` (bsdtar) | Provided by Windows. `tar.exe` is also run at run time to unpack the downloaded FFmpeg archive. |

## 4. Assets

| Asset | Origin | License |
|---|---|---|
| Logo and application icon (`res/icons/logo.svg`, `res/app.ico`) | Original artwork created for ShaderToMP4 | GPL-3.0-or-later |
| Interface icons (`res/icons/*.svg`) | Original drawings made for the project in a geometric stroke style (24 × 24 grid, 2 px stroke). They are simple, generic pictograms (arrows, folders, eye, play...) and are not copied from an icon set. | GPL-3.0-or-later |
| Example and test shaders (`examples/`, `tests/`) | Original code of the project | GPL-3.0-or-later |
| Language files (`lang/en.json`, `lang/fr.json`) | Original texts of the project | GPL-3.0-or-later |
| Screenshots and banner (`docs/`) | Produced from the program itself | GPL-3.0-or-later |

## 5. Rules for future components

Before adding a dependency (library, font, icon set, tool downloaded at run time):

1. Check that its license is compatible with the GPL-3.0-or-later (MIT, BSD, ISC, Zlib, Apache-2.0, LGPL, MPL-2.0, GPL-3.0, SIL OFL for fonts: yes; "non-commercial", "no derivatives", GPL-2.0-only, proprietary: **no**).
2. Add a row to this file (English) and to [THIRD_PARTY_NOTICES.fr.md](THIRD_PARTY_NOTICES.fr.md) (French), with the license and the way it is used.
3. Show it in the "About" window (button "Third-party notices").
4. Keep the program portable: nothing is installed outside the application folder.
