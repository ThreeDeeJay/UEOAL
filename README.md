# UEOAL — Unreal Engine OpenAL Soft 3D Audio Spatializer

[![Build UEOAL](https://github.com/yourusername/UEOAL/actions/workflows/build.yml/badge.svg)](https://github.com/yourusername/UEOAL/actions/workflows/build.yml)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

**UEOAL** is a drop-in `XAudio2_9.dll` proxy that intercepts Unreal Engine 4/5
game audio and re-routes 3D spatial sounds through
[OpenAL Soft](https://openal-soft.org/) for object-based **binaural HRTF
rendering** over headphones — without modifying or recompiling any game.

---

## How it works

```
UE4/5 game
  │
  ├─ XAudio2Create()          ← intercepted by UEOAL (DLL proxy)
  │
  ├─ X3DAudioCalculate()      ← hooked by UEOAL (MinHook)
  │     emitter pos / vel     →  stored per audio thread
  │     listener pos / orient →  forwarded to AL listener
  │
  ├─ IXAudio2SourceVoice
  │     ::SetOutputMatrix()   ← UEOAL associates emitter → AL source
  │     ::SubmitSourceBuffer()← UEOAL feeds PCM to AL source (queued streaming)
  │     ::Start / Stop / etc. ← forwarded to both XAudio2 and OpenAL
  │
  └─ OpenAL Soft (OpenAL32.dll)
        AL_HRTF_SOFT enabled
        per-source 3-D positioning (UE ↔ AL coordinate conversion)
        binaural rendering → headphone output
```

XAudio2 continues to run normally alongside OpenAL; the game's own pipeline is
unaffected, ensuring stability while OpenAL provides the HRTF spatialization.

---

## Installation

### Pre-built release

1. Download the ZIP for your game's architecture from
   [Releases](../../releases/latest).
   Most modern UE games are **64-bit** → choose `UEOAL-*-x64.zip`.
2. Extract the ZIP.  You will find:
   - `XAudio2_9.dll` — the UEOAL proxy
   - `OpenAL32.dll`  — OpenAL Soft runtime (HRTF-enabled build)
   - `README.md`, `LICENSE`, `VERSION.txt`
3. Copy **both DLLs** to the game's root directory — the same folder that
   contains the game `.exe`.
4. Launch the game.  HRTF spatial audio is now active.

> **Note:** Windows loads DLLs from the application directory before System32,
> so your `XAudio2_9.dll` will be loaded instead of the system copy.

### Uninstalling

Delete `XAudio2_9.dll` (and `OpenAL32.dll` if you placed it) from the game
folder.  The game will revert to its default audio pipeline.

---

## Verbose logging

Set the environment variable **`UEOAL_LOG_PATH`** to an absolute file path
before launching the game:

```bat
:: Windows CMD
set UEOAL_LOG_PATH=C:\ueoal.log
"C:\Games\MyUEGame\MyGame.exe"
```

```powershell
# PowerShell
$env:UEOAL_LOG_PATH = "C:\ueoal.log"
& "C:\Games\MyUEGame\MyGame.exe"
```

The log records every source voice creation, emitter association, AL buffer
submission, listener update, and lifecycle event at DEBUG granularity.
If `UEOAL_LOG_PATH` is not set, **no logging occurs** and there is zero
file-I/O overhead.

---

## Building from source

### Prerequisites

| Tool | Version |
|---|---|
| Visual Studio 2022 | 17.x with "Desktop development with C++" |
| CMake | ≥ 3.20 |
| Git | any recent |

vcpkg is bootstrapped automatically by the build scripts / GitHub Actions.

### Quick build (PowerShell)

```powershell
# Clone
git clone https://github.com/yourusername/UEOAL.git
cd UEOAL

# Bootstrap vcpkg
git clone https://github.com/microsoft/vcpkg.git --depth 1
.\vcpkg\bootstrap-vcpkg.bat -disableMetrics

# Build x64
cmake -B build-x64 -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="vcpkg\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build-x64 --config Release

# Build x86
cmake -B build-x86 -A Win32 `
  -DCMAKE_TOOLCHAIN_FILE="vcpkg\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x86-windows
cmake --build build-x86 --config Release
```

Output DLL: `build-x64\Release\XAudio2_9.dll`

---

## Coordinate system

Unreal Engine uses a **left-handed, centimetre** coordinate system
(X = forward, Y = right, Z = up).  OpenAL uses a **right-handed, metre**
system (X = right, Y = up, Z = backward).  UEOAL converts automatically:

```
AL.x =  UE.Y × 0.01   (right)
AL.y =  UE.Z × 0.01   (up)
AL.z = -UE.X × 0.01   (−forward = backward)
```

---

## Compatibility

| Engine version | Status |
|---|---|
| UE 4.20 – 4.27 | ✅ Tested |
| UE 5.0 – 5.4   | ✅ Tested |
| UE 4.x (32-bit) | ✅ Supported (use x86 build) |

Games that use XAudio2_8 or XAudio2_7 require renaming the output DLL
accordingly (see `exports.def`).

---

## Project structure

```
UEOAL/
├── src/
│   ├── dllmain.cpp               Entry point, XAudio2Create export shims
│   ├── logger.h / logger.cpp     Thread-safe logger (UEOAL_LOG_PATH env var)
│   ├── version.h.in              CMake-configured version header
│   ├── exports.def               DLL export ordinals
│   ├── audio/
│   │   ├── openal_backend.h      OpenAL Soft backend interface
│   │   └── openal_backend.cpp    HRTF device init, source/listener management
│   ├── hooks/
│   │   ├── x3daudio_hook.h       MinHook-based X3DAudioCalculate intercept
│   │   └── x3daudio_hook.cpp
│   └── proxy/
│       ├── xaudio2_proxy.*       IXAudio2 wrapper
│       ├── source_voice_proxy.*  IXAudio2SourceVoice wrapper (core interception)
│       ├── submix_voice_proxy.*  IXAudio2SubmixVoice pass-through
│       └── mastering_voice_proxy.*  IXAudio2MasteringVoice pass-through
├── .github/workflows/build.yml   CI: builds x64 + x86, packages versioned ZIPs
├── CMakeLists.txt
├── vcpkg.json
└── LICENSE                       GNU GPLv3
```

---

## License

UEOAL is free software released under the
[GNU General Public License v3.0](LICENSE).

Dependencies:
- [OpenAL Soft](https://openal-soft.org/) — LGPL-2.0
- [MinHook](https://github.com/TsudaKageyu/minhook) — BSD-2-Clause
