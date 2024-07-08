# CafeGLSL - Shader Compiler for Wii U

This project is an experimental runtime GLSL shader compiler library for the Wii U. It's implemented as a fork of Mesa. The compiler is currently designed for runtime use, meaning you invoke it from your application to compile shaders on the fly. It's not designed to be used as a standalone tool, but support for this may be added in the future.

Why make this?
By default, Wii U offers no way to compile shaders dynamically during runtime, yet many games, render API translation layers and other dependencies rely on this. This project is intended to fill this void.

## ⚠️ Warning
This library is still in its experimental stage. While it lacks some features and likely contains bugs, it's released early in the hopes that the community can already start benefiting.

### Current Limitations:

- Supports only separable shaders. All binding locations for textures, in/out varyings and uniform buffers need to be explicit
- No support for geometry/compute/tesselation shaders
- No uniform register support, all uniforms are read via buffers. Uniforms declared outside of blocks will be mapped to a virtual block bound to buffer 15.

Shaders may still compile even if you break these rules, but they will likely not function as you expect.

## How to use
There's two methods of using this compiler.

If you need to compile shaders dynamically at runtime on the Wii U, you need to use the .rpl file.

However, if you can compile all required shaders on your PC (as part of your build process, for example), you should probably use the .elf file.
It is currently limited to Linux/WSL/Docker only so setting it up may be a bit more involved then the .rpl method.

### Statically compile shaders to .gsh on PC (Linux/WSL/Docker only, recommended):

#### Compilation:
1. Follow the [build instructions](#how-to-compile) to compile for your OS or see if the precompiled .elf binaries from the [GitHub Releases](https://github.com/Exzap/CafeGLSL/releases) work on your Linux installation.
2. Use the `glslcompiler.elf` binary to compile your shaders to .gsh files. The usage is as follows:
```bash
# Example
glslcompiler -ps ./input/crt.ps -vs ./input/crt.vs -o ./output/crt.gsh

# Usage:
Usage: shader_compiler [options]
Options:
  -ps <file>        : Pixel shader file (can be used multiple times to pack multiple shaders into .gsh file)
  -vs <file>        : Vertex shader file (can be used multiple times  to pack multiple shaders into .gsh file)
  -o <file>         : Output path for .gsh file (default: no file is written)
  -t                : Run tests
  -v                : Verbose output (prints assembly and debug information)
```

#### Usage:

The output will be a .gsh file that you can use in your Wii U project. The .gsh file contains the compiled shaders and can be used with the WHB library:
```c++
// One serialized .gsh with 1 vertex and 1 pixel shader
WHBGfxShaderGroup whbShaderGroup = {};
auto whbShaderGroup = WHBGfxLoadGFDShaderGroup(&whbShaderGroup, 0, shaderFileBuffer.data());

// Two serialized .gsh with 1 vertex and 1 pixel shader each
GX2VertexShader* vs = WHBGfxLoadGFDVertexShader(0, pixelShaderFileBuffer.data());
GX2PixelShader* ps = WHBGfxLoadGFDPixelShader(0, vertexShaderFileBuffer.data());

// Serialized .gsh with 2 vertex and 2 pixel shaders
GX2VertexShader* vs1 = WHBGfxLoadGFDVertexShader(0, shaderFileBuffer.data());
GX2VertexShader* vs2 = WHBGfxLoadGFDVertexShader(1, shaderFileBuffer.data());
GX2PixelShader* ps1 = WHBGfxLoadGFDPixelShader(0, shaderFileBuffer.data());
GX2PixelShader* ps2 = WHBGfxLoadGFDPixelShader(1, shaderFileBuffer.data());
```

### Dynamically compile shaders at runtime on the Wii U (.rpl):

1. Copy [cafecompiler/CafeGLSLCompiler.h](cafecompiler/CafeGLSLCompiler.h) into your project's source directory and include it.
2. Download `glslcompiler.rpl` from the [Github Releases](https://github.com/Exzap/CafeGLSL/releases) and install it using these platform-specific steps.
   - For Aroma: Copy glslcompiler.rpl to `sd:/wiiu/libs/glslcompiler.rpl` (you can customize the location inside the header).
   - For Cemu: Open Cemu's data folder (open Cemu, then use the `File`->`Open Cemu folder` menu option). Create a folder called `cafeLibs` and copy the glslcompiler.rpl into it.
   - For Decaf and Cemu: Copy `glslcompiler.rpl` into the code folder of the title, next to the application's .rpx, app.xml and cos.xml files (mimicking an unpacked title). Note that the shaders currently cause Decaf to assert.

#### Compilation & Usage:
```c
// Initialize the GLSL compiler (requires the glslcompiler.rpl to be installed, see above)
GLSL_Init();

// Example method to compile shaders into a WHBGfxShaderGroup
WHBGfxShaderGroup* GLSL_CompileShader(const char* vsSrc, const char* psSrc) 
{
    char infoLog[1024];
    GX2VertexShader* vs = GLSL_CompileVertexShader(vsSrc, infoLog, sizeof(infoLog), GLSL_COMPILER_FLAG_NONE);
    if(!vs) {
        OSReport("Failed to compile vertex shader. Infolog: %s\n", infoLog);
        return NULL;
    }
    GX2PixelShader* ps = GLSL_CompilePixelShader(psSrc, infoLog, sizeof(infoLog), GLSL_COMPILER_FLAG_NONE);
    if(!ps) {
        OSReport("Failed to compile pixel shader. Infolog: %s\n", infoLog);
        return NULL;
    }
    WHBGfxShaderGroup* shaderGroup = (WHBGfxShaderGroup*)malloc(sizeof(WHBGfxShaderGroup));
    memset(shaderGroup, 0, sizeof(*shaderGroup));
    shaderGroup->vertexShader = vs;
    shaderGroup->pixelShader = ps;
    return shaderGroup;
}
```

## How to compile

Setup a Linux or WSL environment in the usual way for Wii U homebrew development. Then additionally you need:
1. **Meson** - Mesa uses the Meson build system. You can get it from your system package manager.
2. For Meson and Mesa you need these additional system packages (the names may differ on non-Debian based distros):
    - python3, python3-setuptools, python3-mako, bison, flex
3. Additional dependencies from **devkitPro pacman**:
    - `dkp-meson-scripts`
    - `dkp-toolchain-vars`
4. ⚠️ Requires WUT with the changes from [PR 325](https://github.com/devkitPro/wut/pull/325). As of writing this, the PR has not yet been merged. 

Compile glslcompiler.rpl for Wii U using:
```bash
./cafecompiler/compile_for_cafe.sh
```
See the `./build-cafe/cafecompiler/` folder for the output with the .rpl file.

Compile glslcompiler.elf for PC using:
```bash
./cafecompiler/compile_for_host.sh
```
See the `./build-host/cafecompiler/` folder for the output with the .elf file.

## Troubleshooting and contributing

If you have any problems using this, please open a github issue. I'll try to help as much as I can.  
I would also appreciate any help with this project and PRs are very welcome!

## 📜 License

For original Mesa code see [https://docs.mesa3d.org/license.html](https://docs.mesa3d.org/license.html)  
Any additions by this fork are licensed under MIT.
Thanks to exjam for granting permission to use Decaf's libgfd code.