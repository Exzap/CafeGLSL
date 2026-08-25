# CafeGLSL @VERSION@ - Wii U library

A GLSL to GX2 shader compiler for the Wii U (Latte) GPU, based on Mesa,
cross-compiled for CafeOS with devkitPPC and wut.

## Install

The tree is laid out as a devkitPro portlib, so it can be extracted straight
over the wiiu portlibs prefix:

    tar -xf <this-archive> -C /opt/devkitpro/portlibs/wiiu --strip-components=1

From there either build system finds it. With CMake:

    find_package(cafeglsl REQUIRED)
    target_link_libraries(myapp PRIVATE cafeglsl::cafeglsl)

With pkg-config:

    powerpc-eabi-pkg-config --cflags --libs cafeglsl

The final link needs `-Wl,--gc-sections`. wut.specs already passes it, and the
CMake package sets it as well.

## Compile shaders at runtime

    #include <cafeglsl/CafeGLSLCompiler.h>

Call `InitGLSLCompiler()` once, then `CompileVertexShader()` and
`CompilePixelShader()` with GLSL source. Both hand back the `GX2VertexShader`
and `GX2PixelShader` structs GX2 already expects, so there is no .gsh file in
between and nothing to load off the SD card.

Free them with `FreeVertexShader()` and `FreePixelShader()`, and call
`DestroyGLSLCompiler()` when you are done.

## Cache the result as .gsh

Compiling is not cheap, so an application that always builds the same shaders
may prefer to compile once and keep the result:

    #include <cafeglsl/gfd.h>

Fill in a `GFDFile` and call `WriteGFD()` to write a .gsh to the SD card, then
load it on later runs with `WHBGfxLoadGFDShaderGroup` and skip the compiler
entirely. Both headers are served by the same library.

## Compile shaders offline instead

If the shaders are known ahead of time, the desktop release compiles them on
your PC into .gsh files. That keeps this compiler out of your RPX, which is
worth a lot given its size.

## License

Licensed under the MIT license, as Mesa is.
See LICENSE, and docs/license.rst in the source tree for the full third-party breakdown.
