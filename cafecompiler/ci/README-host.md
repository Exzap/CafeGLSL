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

## License

Licensed under the MIT license, as Mesa is.
See LICENSE, and docs/license.rst in the source tree for the full third-party breakdown.
