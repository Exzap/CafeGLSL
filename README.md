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

## Usage

To use the shader compiler:

1. Copy `cafecompiler/CafeGLSLCompiler.h` into your project's source directory and include it.
2. Install `glslcompiler.rpl`
   - For Aroma: Copy glslcompiler.rpl to `sd:/wiiu/libs/glslcompiler.rpl`
   - For Cemu: Find the Cemu data folder, on Windows this is where Cemu.exe is. Create a folder called `cafeLibs` and copy glslcompiler.rpl into it.
   - For Decaf: Copy `glslcompiler.rpl` into the code folder of the title. But note that the shaders currently cause Decaf to assert.

Currently, the shader compiler is only available as a Wii U dynamic library (.rpl)

### Example:

To initialize the compiler:
```c
GLSL_Init();
```

To compile shaders:
```c
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

**Note**: If you just want to use the shader compiler in your project, you can grab a precompiled binary from the releases page.

Setup a Linux or WSL environment in the usual way for Wii U homebrew development. Then additionally you need:
1. **Meson** - Mesa uses the Meson build system. You can get it from your system package manager.
2. For Meson and Mesa you need these additional system packages (the names may differ on non-Debian based distros):
    - python3, python3-setuptools, python3-mako, bison, flex
3. Additional dependencies from **devkitPro pacman**:
    - `dkp-meson-scripts`
    - `dkp-toolchain-vars`
4. ⚠️ Requires WUT with the changes from [PR 325](https://github.com/devkitPro/wut/pull/325). As of writing this, the PR has not yet been merged. 

Compile for Wii U using:
   ```bash
   ./cafecompiler/compile_for_cafe.sh
   ```

## Troubleshooting and contributing

If you have any problems using this, please open a github issue. I'll try to help as much as I can.  
I would also appreciate any help with this project and PRs are very welcome!

## 📜 License

For original Mesa code see [https://docs.mesa3d.org/license.html](https://docs.mesa3d.org/license.html)  
Any additions by this fork are licensed under MIT.