# Building CafeGLSL

## Requirements

Mesa uses Meson and Ninja. You'll also need a C/C++ compiler and the Python
and system libraries listed in the [build workflow](.github/workflows/build.yml).
The workflow contains the full setup and test commands for each platform.

For the Wii U library, also install devkitPro with the Wii U development tools and wut.

## Compilation commands

Compile the desktop compiler and static library using:

```bash
./cafecompiler/compile_for_host.sh
```

See `build-host/cafecompiler/` for `glslcompiler` and `libcafeglsl.a`.
Windows builds use `glslcompiler.exe`.

Compile the Wii U static library using:

```bash
./cafecompiler/compile_for_cafe.sh
```

The library is written to `build-cafe/cafecompiler/libcafeglsl.a`.
