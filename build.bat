@echo off
set "PATH=C:\msys64\mingw32\bin;%PATH%"
gcc -O2 -I native_port native_port/elf_loader.c native_port/fake_jni.c native_port/gl_wrap.c native_port/gl_buffers.c native_port/shims.c native_port/audio.c native_port/mesh_dump.c native_port/main.c -o RagingThunder2_Windows.exe -lmingw32 -lSDL2main -lSDL2 -lopengl32 -lws2_32
if %ERRORLEVEL% EQU 0 (
    echo [BUILD SUCCESS] RagingThunder2_Windows.exe compiled successfully!
) else (
    echo [BUILD FAILED] Compilation failed with code %ERRORLEVEL%
)
