# CafeGLSL @VERSION@ - standalone shader compiler

A GLSL to GX2 shader compiler for the Wii U (Latte) GPU, based on Mesa.

## Compile shaders offline on Windows, Linux or macOS (CLI)

The CLI writes shaders to a .gsh file, the standard container format for Wii U
shaders. Load one with the WHB helpers: `WHBGfxLoadGFDShaderGroup` for a vertex
and pixel shader pair, or `WHBGfxLoadGFDVertexShader` and
`WHBGfxLoadGFDPixelShader` for one at a time.

The binary is in `bin/`:

    # Compile as a pair: both use uniform block mode if either needs it
    @BINARY@ -vs shader.vert -ps shader.frag -o shaders.gsh

    # Compile single and multiple shaders separately: their uniform modes may differ
    @BINARY@ -vs shader.vert -ps shader.frag -vs shader2.vert -ps shader2.frag -o shaders.gsh

Options:

    -vs <file>  compile a vertex shader
    -ps <file>  compile a pixel shader
    -o  <file>  write the results to a GFD (.gsh) file
    -v          print the R600 disassembly

    --uniform-mode <auto|register|block>
                auto      use registers when possible, blocks when needed (default)
                register  use registers, fail if blocks are needed
                block     use blocks, moving loose uniforms into a block

The first example shows the recommended way to compile shaders, since it guarantees that both will use the same uniform mode.
If one needs uniform blocks, the other uses block mode too.
The compiler warns if this moves loose uniforms out of registers, as you'll need to upload them at binding 0 using `GX2Set*UniformBlock` instead of `GX2Set*UniformReg`.

Only one vertex shader and one pixel shader compile as a pair. All other combinations compile separately, as in the second example.
When combining separately compiled shaders, make sure both use the same uniform mode.

The CLI prints the mode once for a pair, or once per shader in input order
when compiled separately. For example:

    @BINARY@ -vs shader.vert -ps shader.frag -o shaders.gsh
    GX2_SHADER_MODE_UNIFORM_BLOCK

This means you'll need to call `GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_BLOCK)`
before drawing with these shaders. See
[Important: choose loose uniforms or uniform blocks](#important-choose-loose-uniforms-or-uniform-blocks)
below, especially if you compile the shaders separately.

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

Call `InitGLSLCompiler()` before compiling and `DestroyGLSLCompiler()` when you no longer need the compiler.

We recommend compiling the vertex and pixel shaders together so they use the same shader mode:

```cpp
char log[4096];
GX2VertexShader *vertex = NULL;
GX2PixelShader *pixel = NULL;
GX2ShaderMode mode = CompileShaderPair(
    vertexSource, pixelSource, GLSL_COMPILE_AUTO, &vertex, &pixel,
    log, sizeof(log), GLSL_COMPILER_FLAG_NONE
);
```

The return value tells you which mode to use with `GX2SetShaderMode()` on the Wii U.
If compilation fails, it returns `GLSL_SHADER_MODE_ERROR` instead and writes the error message to `log`.
The log can also contain warnings when compilation succeeds.
See [Important: choose loose uniforms or uniform blocks](#important-choose-loose-uniforms-or-uniform-blocks) below for how the mode is chosen.

You can also compile shaders separately with `CompileVertexShader()` and `CompilePixelShader()`.
They take the same mode option and return the mode of that shader.
Free shaders when you are done with `FreeShaders(vertex, pixel)`, or use `FreeVertexShader()` and `FreePixelShader()` separately.

## Compile shaders on the Wii U itself (Wii U static library)

To compile shaders at runtime on hardware, see the instructions inside the
Wii U release instead.

## Important: choose loose uniforms or uniform blocks

Just a small GLSL refresher: uniform values can be declared on their own
("loose uniforms") or grouped in a block ("uniform blocks"):

```glsl
uniform vec4 color;

layout(std140) uniform Material {
    vec4 color;
};
```

On the Wii U, the first form uses `GX2Set*UniformReg` to change its values when compiled in register mode.
The second form uses `GX2Set*UniformBlock` instead.

Note that GX2 uses `GX2_SHADER_MODE_UNIFORM_REGISTER` by default. To use uniform blocks,
you __have__ to select the uniform block mode before drawing:

```cpp
GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_BLOCK);
```

If you don't use loose uniforms, you don't need to worry about their conversion.
You can compile all shaders with `GLSL_COMPILE_BLOCK` and keep GX2 in this mode,
including when one shader has no uniforms.

To go back to being able to call loose uniforms, you'll have to call
`GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER)` again.
The Wii U reads uniform values as zero when the wrong mode is selected.

This also means that you can't mix loose uniforms and uniform blocks in a single draw.
If you write the shaders yourself, move the loose uniforms into a block to avoid this issue.

If you cannot change the source code (e.g. translation layers), the auto and block modes can
move loose uniforms into a `__cafe_loose_uniforms` uniform block at binding 0.
And to upload to the shader, you'll have to use `GX2Set*UniformBlock` instead.
Binding 0 must be free in any shader where loose uniforms need to be moved.
Use the uniform offsets and block sizes recorded in the compiled shader to
place the values in the buffer. This is an advanced use case.

Loose uniforms in register mode can contain at most 1024 scalar components.
For example, this allows 1024 `int` values or 256 `vec4` values. Automatic mode
selection also moves loose uniforms into a block when they exceed this limit.

All compile functions let you choose how to handle uniforms:

- `GLSL_COMPILE_AUTO` uses register mode when possible and uniform block mode
  when needed. When compiling a pair, both shaders use uniform block mode if
  either shader needs it.
- `GLSL_COMPILE_REGISTER` uses register mode. Compilation fails if a shader
  has uniform blocks or too many loose uniforms.
- `GLSL_COMPILE_BLOCK` uses uniform block mode, moving any loose uniforms into
  a block.

These are equivalent to the `--uniform-mode auto`, `--uniform-mode register`,
or `--uniform-mode block` options in the CLI.

If you rely on automatic uniform mode selection and compile shader stages separately,
you'll need to manage the issue of having both the vertex and pixel shaders share the same mode,
since the shader mode inside `GX2SetShaderMode()` is used for the entire draw call.

For example, a vertex shader with no uniforms, or just a loose `uniform vec4`,
selects register mode when compiled with AUTO. A pixel shader with a uniform
block selects uniform block mode.

Even if the vertex shader has no uniforms, choosing the draw's mode from that
shader alone would be wrong. The pixel shader still needs uniform block mode.

`CompileShaderPair()` chooses uniform block mode for both in this example.
If the vertex shader has a loose uniform, it moves that value into a block.
You'll then upload it with `GX2SetVertexUniformBlock` instead of `GX2SetVertexUniformReg`.

If you compile them separately, compile the vertex shader with
`GLSL_COMPILE_BLOCK` to get the same result.

## License

Licensed under the MIT license, as Mesa is.
See [LICENSE](LICENSE) and [Mesa's license information](https://docs.mesa3d.org/license.html) for third-party licensing.
