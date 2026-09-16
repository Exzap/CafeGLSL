# CafeGLSL - Shader Compiler for Wii U

CafeGLSL is a GLSL shader compiler for the Wii U, built on Mesa. You can use it to compile shaders on the fly inside your application, or compile them ahead of time on Windows, Linux or macOS.

**Why make this?**

By default, Wii U offers no way to compile shaders dynamically during runtime, yet many games, render API translation layers and other dependencies rely on this. This project is intended to fill this void.

## Current limitations

- No support for geometry, compute or tessellation shaders.
- Wii U limitation: GX2 uses one uniform mode for both the vertex and pixel shader in each draw. You must select that mode before drawing. See [Important: choose loose uniforms or uniform blocks](#important-choose-loose-uniforms-or-uniform-blocks) below.

## How to use

There are two ways to use this compiler.

If you need to compile shaders dynamically at runtime on the Wii U, use the
static library. You can also embed the desktop library in your own tools.

However, if you can compile all required shaders on your PC (as part of your build process, for example),
you can also use the standalone compiler to compile to .gsh files.

### Statically compile shaders to .gsh on PC (Windows, Linux or macOS):

#### Compilation:

1. Download a [precompiled release](https://github.com/Exzap/CafeGLSL/releases) or follow the [build instructions](BUILDING.md) to compile for your OS.
2. Use the binary in `bin/` to compile your shaders to .gsh files. On Windows, use `glslcompiler.exe`.

```bash
# Compile as a pair: both use uniform block mode if either needs it
bin/glslcompiler -vs shader.vert -ps shader.frag -o shaders.gsh

# Compile single and multiple shaders separately: their uniform modes may differ
bin/glslcompiler -vs shader.vert -ps shader.frag -vs shader2.vert -ps shader2.frag -o shaders.gsh
```

Options:

```text
-vs <file>  compile a vertex shader
-ps <file>  compile a pixel shader
-o  <file>  write the results to a GFD (.gsh) file
-v          print the R600 disassembly

--uniform-mode <auto|register|block>
            auto      use registers when possible, blocks when needed (default)
            register  use registers, fail if blocks are needed
            block     use blocks, moving loose uniforms into a block
```

The first example shows the recommended way to compile shaders, since it guarantees that both will use the same uniform mode.
With the default `auto` mode, if one needs uniform blocks, the other uses block mode too.
The compiler warns if this moves loose uniforms out of registers, as you'll need to upload them at binding 0 using `GX2Set*UniformBlock` instead of `GX2Set*UniformReg`.

Only one vertex shader and one pixel shader compile as a pair. All other combinations compile separately, as in the second example.
When combining separately compiled shaders, make sure both use the same uniform mode.

#### Usage:

The output will be a .gsh file that you can use in your Wii U project.
Load a vertex and pixel shader pair with the WHB library:

```cpp
WHBGfxShaderGroup shaderGroup = {};
WHBGfxLoadGFDShaderGroup(&shaderGroup, 0, shaderFileBuffer.data());
```

Use `WHBGfxLoadGFDVertexShader()` and `WHBGfxLoadGFDPixelShader()` to load shaders
individually. The first argument selects the shader of that type, starting at 0.

The CLI prints the mode once for a pair, or once per shader in input order when compiled separately. For example:

```text
bin/glslcompiler.exe -vs shader.vert -ps shader.frag -o shaders.gsh
GX2_SHADER_MODE_UNIFORM_BLOCK
```

This means you'll need to call `GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_BLOCK)` before drawing with these shaders.
See [Important: choose loose uniforms or uniform blocks](#important-choose-loose-uniforms-or-uniform-blocks) below,
especially if you compile the shaders separately.

### Dynamically compile shaders at runtime on the Wii U (static library):

#### Installation:

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

#### Compilation & Usage:

```cpp
#include <cafeglsl/CafeGLSLCompiler.h>
```

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

### Embed the compiler in a desktop application:

The desktop release includes `lib/libcafeglsl.a` and headers in
`include/cafeglsl/`. It uses the same compile functions as the Wii U library.

To save shaders to a .gsh file, include `<cafeglsl/gfd.h>`, fill in a `GFDFile` and call `WriteGFD()`.
Both headers are served by the same library.
You can also save shaders compiled on the Wii U and load them on later runs.

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

If you cannot change the source code (e.g. translation layers), CafeGLSL can (when using auto/block compile modes)
move the loose uniforms into a `__cafe_loose_uniforms` uniform block at binding 0.
And to upload to the shader, you'll have to use `GX2Set*UniformBlock` instead.
Binding 0 must be free in any shader where loose uniforms need to be moved.
Use the uniform offsets and block sizes recorded in the compiled shader to
place the values in the buffer. This is definitely more of an advanced use-case.

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

For example, a vertex shader without any uniforms, or only a loose `uniform vec4` (when using the "auto" compile mode) will use the register mode.
But then when you compile a pixel shader with a uniform block, it'll select uniform block mode.

Even when only the pixel shader uses uniforms, choosing the mode from the vertex shader alone would be wrong.
If your application does this, it will select register mode when the pixel shader needs uniform block mode.

Now, if you used `CompileShaderPair()`, in this example it would've moved the vertex shader's loose uniform into a block so both can be used together.
You'll still have to upload that value with `GX2SetVertexUniformBlock` instead of `GX2SetVertexUniformReg`.

If you compile them separately, recompile the vertex shader with `GLSL_COMPILE_BLOCK` to get the same result.

## Building from source

See [BUILDING.md](BUILDING.md) for dependencies and build instructions.

## Troubleshooting and contributing

If you have any problems using this, please open a GitHub issue. I'll try to help as much as I can.
I would also appreciate any help with this project and PRs are very welcome!

## License

For original Mesa code see [docs/license.rst](docs/license.rst).
Any additions by this fork are licensed under MIT.
Thanks to exjam for granting permission to use Decaf's libgfd code.
