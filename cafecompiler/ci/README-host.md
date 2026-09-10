# CafeGLSL @VERSION@ - standalone shader compiler

A GLSL to GX2 shader compiler for the Wii U (Latte) GPU, based on Mesa.

## Compile shaders offline on Windows, Linux or macOS (CLI)

The CLI writes shaders to a .gsh file, the standard container format for Wii U
shaders. Load one with the WHB helpers: `WHBGfxLoadGFDShaderGroup` for a vertex
and pixel shader pair, or `WHBGfxLoadGFDVertexShader` and
`WHBGfxLoadGFDPixelShader` for one at a time.

The binary is in `bin/`:

    @BINARY@ -vs shader.vert -ps shader.frag -o shaders.gsh

Options:

    -vs <file>  compile a vertex shader
    -ps <file>  compile a pixel shader
    -o  <file>  write the results to a GFD (.gsh) file
    -v          print the R600 disassembly
    --allow-uniform-block-fallback
                move loose uniforms into a uniform block when needed

## Compile shaders inside an existing application (desktop static library)

You can also embed the compiler into a desktop application, like a modding
tool, and compile shaders without shelling out to the CLI.

The library is `lib/libcafeglsl.a`, with its headers in `include/cafeglsl/`:

    #include <cafeglsl/CafeGLSLCompiler.h>

`CompileVertexShader()` and `CompilePixelShader()` hand back the same
`GX2VertexShader` and `GX2PixelShader` structs the CLI produces. To serialize
those to a .gsh file the way the CLI does, add:

    #include <cafeglsl/gfd.h>

then fill in a `GFDFile` and call `WriteGFD()`. Both headers are served by the
same library, so there is nothing extra to link.

## Compile shaders on the Wii U itself (Wii U static library)

To compile shaders at runtime on hardware, see the instructions inside the
Wii U release instead.

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
`GLSL_COMPILER_FLAG_ALLOW_UNIFORM_BLOCK_FALLBACK` in the library or use
`--allow-uniform-block-fallback` in the CLI. CafeGLSL then moves the loose uniforms
into a `__cafe_loose_uniforms` uniform block at binding 0.
And to upload to the shader, you'll have to use `GX2Set*UniformBlock` instead.

Loose uniforms can contain at most 1024 scalar components. For example, this
allows 1024 `int` values or 256 `vec4` values. This option also moves loose
uniforms into a block when they exceed this limit.

## License

Licensed under the MIT license, as Mesa is.
See LICENSE, and docs/license.rst in the source tree for the full third-party breakdown.
