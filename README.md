# Raging Thunder 2 — Native Windows Port & Recompilation

A native desktop Windows port and binary recompilation of **Raging Thunder 2 Lite** (`raging-thunder-21-0-16.apk`, Polarbit Fuse engine) running the authentic x86 Android library (`apk/lib/x86/librthunder2lite.so`) directly on Windows via native WOW64, desktop OpenGL, and SDL2.

Rather than an approximation or incomplete engine rewrite, this port executes the **genuine Polarbit game binary** in-process on Windows. All game mechanics, car physics, artificial intelligence, track animations, camera systems, menus, and audio play directly from Polarbit's original compiled machine code.

---

## Architecture Overview

```
+---------------------------------------------------------------+
|                  Raging Thunder 2 (Game Client)               |
|            Polarbit Fuse Engine (librthunder2lite.so)         |
+-------------------------------+-------------------------------+
                                |
          +---------------------+---------------------+
          |                     |                     |
          v                     v                     v
   [ELF Loader]           [Fake JNI ABI]        [Calling Convention]
  In-process ELF32      Authentic JDK 17 /     cdecl -> stdcall bridge
  dynamic relocation    Dalvik vtable layout    142 OpenGL wrappers
          |                     |                     |
          +---------------------+---------------------+
                                |
                                v
               +---------------------------------+
               |     Native Host Windows Shims   |
               |  Bionic Libc / Win32 / POSIX FS |
               +----------------+----------------+
                                |
                                v
          +---------------------+---------------------+
          |                     |                     |
          v                     v                     v
     [SDL2 Video]         [SDL2 Audio]         [SDL2 Input]
   OpenGL 1.1 Desktop   44.1kHz Stereo PCM   Keyboard, Mouse HUD,
   Hardware VSync 60FPS AudioMix Driver      Xbox/PS Gamepads
```

### 1. In-Process ELF Dynamic Loader (`native_port/elf_loader.c`)
- Loads the 32-bit x86 ELF binary into executable memory under Windows WOW64.
- Resolves ELF segments, sections, GNU hash tables, string tables, and dynamic symbol tables.
- Applies `R_386_32`, `R_386_PC32`, `R_386_GLOB_DAT`, and `R_386_JMP_SLOT` relocations.
- Executes `DT_INIT` and all `.init_array` static constructors.

### 2. Authentic JNI 1.6 Environment (`native_port/fake_jni.c`)
- Implements the exact byte-for-byte Dalvik / JDK 17 `JNINativeInterface` vtable (233 function pointers).
- Maps critical JNI functions at their canonical offsets:
  - `NewIntArray` (slot 179 / `0x2cc`)
  - `SetIntArrayRegion` (slot 211 / `0x34c`)
  - `GetDirectBufferAddress` (slot 230 / `0x398`)
  - `DeleteLocalRef` (slot 23 / `0x6c`)
  - String UTF handlers, array regions, and class constructor dispatch.
- Emulates Android system properties: IMEI, Operator, Device model (`PC`), OS version (`19`).

### 3. Calling Convention Stack Bridge (`native_port/gl_wrap.c`)
- **Problem:** On 32-bit Windows, `opengl32.dll` uses `__stdcall` (callee pops arguments), whereas Linux/Android x86 ELF binaries use `__cdecl` (caller cleans stack). Direct invocation causes stack misalignment and immediate crashes upon return.
- **Solution:** A 142-function `__cdecl` wrapper table intercepts every OpenGL 1.1, extension, and fixed-point ES call, keeping the caller stack 100% aligned.

### 4. Real-Time Audio Engine (`native_port/audio.c`)
- Hooks `Java_com_polarbit_fuse_Jni_AudioMix` to stream Polarbit's native PCM output.
- Outputs 44,100 Hz 16-bit stereo sound through SDL2.
- Music, menu sound effects, and engine revs play in real time.

### 5. Input & Controls (`native_port/main.c`)
- Full support for keyboard, mouse touch emulation, and USB/Bluetooth game controllers (Xbox Series, Xbox One/360, PlayStation DualShock/DualSense).

---

## Controls

| Action | Keyboard | Gamepad (Xbox / PlayStation) | Mouse |
| :--- | :--- | :--- | :--- |
| **Accelerate / Select** | `Enter`, `Space`, `X` | Button `A` / `Cross` | — |
| **Brake / Reverse** | `Down Arrow`, `S`, `Z` | Button `B` / `Circle`, `LB` / `L1` | — |
| **Nitro Boost** | `Up Arrow`, `W`, `C`, `Shift`| Button `Y` / `Triangle`, `RB` / `R1` | — |
| **Steer Left** | `Left Arrow`, `A` | Left Analog Stick Left, D-Pad Left | — |
| **Steer Right** | `Right Arrow`, `D` | Left Analog Stick Right, D-Pad Right| — |
| **Back / Pause Menu** | `Esc`, `Backspace` | `Start`, `Back` / `Options` | — |
| **Menu / Touch HUD** | — | — | Left Click & Drag |

---

## Building and Running

### Prerequisites
- Windows 10 / 11 (64-bit with WOW64 support)
- MinGW32 GCC (MSYS2 `mingw32` toolchain)
- SDL2 development libraries for MinGW32 (included / linked dynamically via `SDL2.dll`)

### Build
Run the build script in Command Prompt or PowerShell:
```cmd
build.bat
```
This compiles `RagingThunder2_Windows.exe` with `-O2` optimizations.

### Run
Launch via `run.bat` or directly:
```cmd
run.bat
```
or:
```cmd
RagingThunder2_Windows.exe
```

### Video Options
The game renders natively at whatever resolution the window is, so a larger window or fullscreen gives a true high-resolution image rather than a stretched one.

| Option | Default | Description |
| :--- | :--- | :--- |
| `--width <px>` / `--height <px>` | `1024` / `600` | Initial window size |
| `--fullscreen` | off | Borderless fullscreen at desktop resolution |
| `--msaa <n>` | `4` | Multisample anti-aliasing samples (`0` disables) |
| `--aniso <n>` | `16` | Anisotropic filtering level (clamped to the GPU maximum) |
| `--no-mipmaps` | — | Keep the original non-mipmapped texture filtering |

Press `F11` or `Alt+Enter` in game to toggle fullscreen. Example:
```cmd
RagingThunder2_Windows.exe --fullscreen --msaa 8
```

---

## Project Structure

```
├── apk/                             # Original Android APK assets & binary
│   ├── assets/Data.vfs              # Polarbit Virtual File System archive (16.3 MB)
│   └── lib/x86/librthunder2lite.so  # Polarbit Fuse x86 native game library
├── native_port/                     # Native Windows host & recomp source
│   ├── audio.c / audio.h            # SDL2 audio pipeline & JNI AudioMix bridge
│   ├── elf_loader.c / elf_loader.h  # In-process ELF dynamic loader & relocator
│   ├── fake_jni.c / fake_jni.h      # Dalvik JNI 1.6 ABI vtable implementation
│   ├── gl_buffers.c / gl_buffers.h  # Buffer ID virtualization & tracking
│   ├── gl_wrap.c / gl_wrap.h        # cdecl <-> stdcall OpenGL wrapper table
│   ├── main.c                       # SDL2 window, event loop, and game orchestration
│   └── shims.c / shims.h            # Bionic POSIX libc/libm runtime shims
├── saves/                           # Profile, settings, and runtime save files
├── build.bat                        # MinGW32 automated build script
├── run.bat                          # Quick launcher script
├── SDL2.dll                         # 32-bit SDL2 runtime library
└── RagingThunder2_Windows.exe       # Final compiled Windows game binary
```
