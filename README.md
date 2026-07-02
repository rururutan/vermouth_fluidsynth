# Vermouth FluidSynth

Vermouth FluidSynth is a drop-in replacement for the original [Vermouth](http://retropc.net/yui/hoot/) DLL interface used by [hoot](http://dmpsoft.s17.xrea.com/hoot).

The original Vermouth synthesizer is a TiMidity-style software synthesizer. This fork keeps the public DLL interface compatible with hoot, but replaces the MIDI rendering backend with [FluidSynth](https://www.fluidsynth.org/) so that standard SF2 SoundFont files can be used.

## Features

- Compatible with the existing `vermouth.dll` interface expected by hoot
- SF2 SoundFont playback through FluidSynth
- In-memory rendering only; no FluidSynth audio device is opened by the DLL
- Optional key display window inherited from the original Vermouth codebase

## Runtime Requirements

- Windows 10 or later by default
- hoot (`hoot.exe`)
- `vermouth.dll`
- FluidSynth runtime DLLs, including `libfluidsynth-3.dll` and its dependencies
- An SF2 SoundFont file

## Installation

Copy the following files to the same directory as `hoot.exe`:

```text
vermouth.dll
vermouth.ini
libfluidsynth-3.dll
FluidSynth dependency DLLs
```

Edit `vermouth.ini` and set the SoundFont path:

```ini
[SF2]
File=soundfont.sf2
```

Relative paths are resolved from the hoot working directory. Absolute paths and environment-variable-expanded paths are also supported by the DLL loader code.

## Building

### Prerequisites

- Visual Studio 2022 with the C++ desktop workload
- CMake 3.20 or later
- vcpkg
- FluidSynth installed through vcpkg

Install FluidSynth for the required triplets:

```bat
vcpkg install fluidsynth:x64-windows
vcpkg install fluidsynth:x86-windows
```

### Configure

Use the vcpkg CMake toolchain file when configuring the project.

For x64:

```bat
cmake -S . -B build-x64 -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
  -DVCPKG_TARGET_TRIPLET=x64-windows
```

For x86:

```bat
cmake -S . -B build-x86 -A Win32 ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
  -DVCPKG_TARGET_TRIPLET=x86-windows
```

### Build

```bat
cmake --build build-x64 --config Release
cmake --build build-x86 --config Release
```

The DLL is generated under:

```text
build-*/bin/Release/vermouth.dll
build-*/bin/Release/libfluidsynth-3.dll
```

## Customization

### Windows 7 Target

The default build targets modern Windows:

```cmake
WINVER=0x0A00
```

To build for Windows 7 or later, change it to:

```cmake
WINVER=0x0601
```

This only changes the SDK target macros. Runtime compatibility still depends on the selected FluidSynth build and its dependencies.

### Disable the Key Display Window

Comment out the following definition in `src/Win9x/compiler.h`:

```cpp
#define SUPPORT_KEYDISP
```

## License

This project is distributed under the same license as the original Vermouth project.

See `LICENSE` for details.

## Acknowledgements

This project is based on the original Vermouth codebase. All credit for the original implementation belongs to its original authors and contributors.

FluidSynth is used as the replacement MIDI rendering backend.
