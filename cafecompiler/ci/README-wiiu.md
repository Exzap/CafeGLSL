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

## Important: choose loose uniforms or uniform blocks

Just a small GLSL refresher: uniform values can be declared on their own
("loose uniforms") or grouped in a block ("uniform blocks"):

    uniform vec4 color;

    layout(std140) uniform Material {
        vec4 color;
    };

On the Wii U, the first form uses `GX2Set*UniformReg` to change its values.
The second form uses `GX2Set*UniformBlock` instead.

Note that GX2 uses `GX2_SHADER_MODE_UNIFORM_REGISTER` by default. To use uniform blocks,
you __have__ to select the uniform block mode before drawing:

    GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_BLOCK);

To go back to being able to call loose uniforms, you'll have to call
`GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER)` again.
The Wii U reads uniform values as zero when the wrong mode is selected.

This also means that you can't mix loose uniforms and uniform in a single shader.
CafeGLSL reports an error by default when one shader mixes them.
If you write the shaders yourself, move the loose uniforms
into a block to avoid this issue.

If you cannot change the source code (e.g. translation layers), enable
`GLSL_COMPILER_FLAG_ALLOW_UNIFORM_BLOCK_FALLBACK` in the library.
CafeGLSL then moves the loose uniforms into a `__cafe_loose_uniforms` uniform block
at binding 0. And to upload to the shader, you'll have to use `GX2Set*UniformBlock` instead.

Loose uniforms can contain at most 1024 scalar components. For example, this
allows 1024 `int` values or 256 `vec4` values. This option also moves loose
uniforms into a block when they exceed this limit.

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
