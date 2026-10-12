# Hardware verification

What was actually tried on real hardware, and what was not. Nothing below is assumed: an untested item is listed as untested.

## Test machine

- Windows 10, one NVIDIA GeForce GT 1030 (OpenGL 3.3 rendering works; the card has no hardware video encoder).
- No AMD or Intel GPU, no Linux machine with a real GPU, no Mac.

## Hardware encoders (Windows, GT 1030)

Each encoder was requested with `encoder=` for H.264, H.265 and AV1, a short render each (12 renders: `nvenc`, `amf`, `qsv` and `auto`, three codecs each).

| Request | Result |
|---|---|
| `nvenc`, `amf`, `qsv`, `auto` × `h264`, `h265`, `av1` | The 0.2 s probe encode fails, the log says the encoder is unavailable, the render continues on the CPU encoder (libx264, libx265, libsvtav1), the file is valid, exit code 0 |

So the CPU fallback is verified for every encoder and codec on a machine that has none of them. That the hardware paths themselves produce a correct video is **not** verified here.

## Not tested (no hardware)

| Item | Why |
|---|---|
| Linux with a real NVIDIA GPU (rendering, NVENC) | No such machine. Linux is covered in CI only (Mesa llvmpipe) |
| Linux with a real AMD GPU (rendering, VAAPI) | No such machine |
| Two-GPU PC (`gpu=high` / `gpu=low`) | No such machine |
| AMD AMF, Intel QuickSync, VAAPI, hardware AV1 | No such GPU; only the fallback was exercised |
| macOS (window, VideoToolbox) | No Mac; hosted CI runners have no GPU, so CI only builds and runs `--version` |
| `endaction=sleep` | Would put the maintainer's PC to sleep during the test; not run |

If you have such hardware, `ShaderToMP4 --diagnostic` and the log of a short render are the most useful things to report in an issue.

## Accessibility and display scaling (Windows)

- NVDA: every button, field, list and slider of the main window and the Output window is announced with a name through MSAA (icon-only buttons included).
- Narrator: the interface tree exposes the same names (checked through UI Automation while Narrator was running); the speech itself cannot be recorded automatically and was not listened to.
- Moving the windows between two screens at 100 % and 150 % scaling: the window is resized to the new scale and stays inside the work area.
