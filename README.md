# CafeGLSL - Shader Compiler for Wii U

This project is an experimental runtime GLSL shader compiler library for the Wii U. It's implemented as a fork of Mesa. The compiler is currently designed for runtime use, meaning you invoke it from your application to compile shaders on the fly. However, a sample standalone binary exists, and can be used to pre-compile `.gsh` shader files.

Why make this?
By default, Wii U offers no way to compile shaders dynamically during runtime, yet many games, render API translation layers and other dependencies rely on this. This project is intended to fill this void.

## ⚠️ Warning
This library is still in its experimental stage. While it lacks some features and likely contains bugs, it's released early in the hopes that the community can already start benefiting.

### Current Limitations:

- Supports only separable shaders. All binding locations for textures, in/out varyings and uniform buffers need to be explicit
- No support for geometry/compute/tesselation shaders
- No uniform register support, all uniforms are read via buffers. Uniforms declared outside of blocks will be mapped to a virtual block bound to buffer 15.
  - Make sure to call `GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_BLOCK);` in your code, or else the Wii U (Cemu is unaffected) won't read your uniform block data.

Shaders may still compile even if you break these rules, but they will likely not function as you expect.

## How to use
There's two methods of using this compiler.

If you need to compile shaders dynamically at runtime on the Wii U, you need to use the .rpl file.

However, if you can compile all required shaders on your PC (as part of your build process, for example), you should probably use the pre-built executable file.
This project has been confirmed to build on Linux, macOS, and Windows. You may be able to build on other platforms, but things may break.

### Statically compile shaders to .gsh on PC:

#### Compilation:

1. Follow the [build instructions](#how-to-compile) to compile for your OS or see if the precompiled executable binaries from the [GitHub Releases](https://github.com/Exzap/CafeGLSL/releases) work on your platform.
2. Use the cafe-glslcompiler executable to compile your shaders to .gsh files. The usage is as follows:

```bash
# Example
cafe-glslcompiler -ps ./input/crt.ps -vs ./input/crt.vs -o ./output/crt.gsh

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

#### Requirements for compiling the executable (for the CLI) or Wii U .rpl file (for use as a runtime library):

1. **Meson** - Mesa uses the Meson build system. You can get it from your system package manager.
2. For Meson and Mesa you need these additional system packages (the names may differ on non-Debian based distros):
    - python3, python3-setuptools, python3-mako, bison, flex
    - for windows you will need to use winbison3-flex instead of bison & flex.
      - You can fetch these from chocolatey or another windows package manager: <https://community.chocolatey.org/packages/winflexbison3>
      - You can also fetch them from the source repository itself: <https://github.com/lexxmark/winflexbison>

#### Additional requirements for compiling a Wii U .rpl file:

3. **devkitPro** - You need to have devkitPro installed. Follow the instructions on their website: [https://devkitpro.org/wiki/Getting_Started](https://devkitpro.org/wiki/Getting_Started)

4. Install the regular Wii U homebrew libraries using `(dkp-)pacman -S wiiu-dev dkp-meson-scripts`. When prompted, install all packages from the `wiiu` group.

5. ⚠️ Requires WUT with the changes from [PR 388](https://github.com/devkitPro/wut/pull/388). As of writing this, the PR has not yet been merged, so you'll need to [build this fork of wut from source](https://github.com/Crementif/wut/tree/new_rpl_fixes?tab=readme-ov-file#building-from-source).

#### Compilation commands:
Compile glslcompiler.rpl for Wii U using:
```bash
./cafecompiler/compile_for_cafe.sh
```
See the `./build-cafe/cafecompiler/` folder for the output with the .rpl file.

Compile cafe-glslcompiler for PC using either the bash script (for non windows sytems), or the powershell script (for windows systems):

```bash
./cafecompiler/compile_for_host.sh
```

```bash
./cafecompiler/compile_for_host.ps1
```

See the `./build-host/cafecompiler/` folder for the output with the .elf file.

## Troubleshooting and contributing

If you have any problems using this, please open a github issue. I'll try to help as much as I can.  
I would also appreciate any help with this project and PRs are very welcome!

## 📜 License

For original Mesa code see [https://docs.mesa3d.org/license.html](https://docs.mesa3d.org/license.html)  
Any additions by this fork are licensed under MIT.
Thanks to exjam for granting permission to use Decaf's libgfd code.
