# CafeGLSL @VERSION@ - Wii U library

A GLSL to GX2 shader compiler for the Wii U (Latte) GPU, based on Mesa,
cross-compiled for CafeOS with devkitPPC and wut.

## Install

The Wii U release is laid out as a devkitPro portlib, so it can be extracted straight into the Wii U portlibs directory:

```bash
tar -xf <this-archive> -C /opt/devkitpro/portlibs/wiiu --strip-components=1
```

Then link it with CMake:

```cmake
find_package(cafeglsl REQUIRED)
target_link_libraries(myapp PRIVATE cafeglsl::cafeglsl)
```

With pkg-config:

```bash
powerpc-eabi-pkg-config --cflags --libs cafeglsl
```

The final link needs `-Wl,--gc-sections`. wut.specs already passes it, and the CMake package sets it as well.

## Compile shaders at runtime

    #include <cafeglsl/CafeGLSLCompiler.h>

Call `InitGLSLCompiler()` once, then `CompileShaderPair()` (or `CompileVertexShader()` and `CompilePixelShader()` separately) with the GLSL source.
These give you the `GX2VertexShader` and `GX2PixelShader` structs GX2 expects when drawing.
You can also save them to a .gsh file using `<cafeglsl/gfd.h>` and load them on later runs.

Call `DestroyGLSLCompiler()` when you no longer need the compiler.

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
See [LICENSE](LICENSE) and [Mesa's license information](https://docs.mesa3d.org/license.html) for third-party licensing.
