#pragma once

#include <stdint.h>

#if defined(__WUT__) || defined(__WIIU__)
#include <gx2/shaders.h>
#else
#include "cafe_gx2.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum GLSL_COMPILER_FLAG {
   GLSL_COMPILER_FLAG_NONE = 0,
   /* Legacy alias for GLSL_COMPILER_FLAG_PRINT_DISASSEMBLY_TO_STDERR. */
   GLSL_COMPILER_FLAG_GENERATE_DISASSEMBLY = 1 << 0,
   GLSL_COMPILER_FLAG_PRINT_DISASSEMBLY_TO_STDERR = 1 << 0,
} GLSL_COMPILER_FLAG;

typedef enum GLSLCompileMode {
   /* Use uniform registers when possible, blocks when needed. */
   GLSL_COMPILE_AUTO = 0,
   /* Fail if uniform blocks are needed. */
   GLSL_COMPILE_REGISTER = 1,
   /* Use uniform blocks, moving loose uniforms into a block. */
   GLSL_COMPILE_BLOCK = 2,
} GLSLCompileMode;

#define GLSL_SHADER_MODE_ERROR ((GX2ShaderMode)-1)

/* Initialize before compiling. Destroy when done. */
void InitGLSLCompiler(void);
void DestroyGLSLCompiler(void);
const char *GetGLSLCompilerVersion(void);

/* Return the mode for GX2SetShaderMode(), or GLSL_SHADER_MODE_ERROR on failure.
 * On failure, shader pointers are set to null and infoLogOut contains the error.
 * See the README's "Important: choose loose uniforms or uniform blocks" chapter. */
GX2ShaderMode CompileVertexShader(const char *source, GLSLCompileMode requestedMode,
                                        GX2VertexShader **shaderOut, char *infoLogOut,
                                        int infoLogMaxLength, GLSL_COMPILER_FLAG flags);
GX2ShaderMode CompilePixelShader(const char *source, GLSLCompileMode requestedMode,
                                       GX2PixelShader **shaderOut, char *infoLogOut,
                                       int infoLogMaxLength, GLSL_COMPILER_FLAG flags);
/* Compile a pair with a shared uniform mode. Both sources are required.
 * Matching vertex outputs to pixel inputs is not checked. */
GX2ShaderMode CompileShaderPair(const char *vertexSource, const char *pixelSource,
                                      GLSLCompileMode requestedMode,
                                      GX2VertexShader **vertexOut, GX2PixelShader **pixelOut,
                                      char *infoLogOut, int infoLogMaxLength,
                                      GLSL_COMPILER_FLAG flags);

/* Free shaders when done. Null shaders are accepted. */
void FreeVertexShader(GX2VertexShader *shader);
void FreePixelShader(GX2PixelShader *shader);
void FreeShaders(GX2VertexShader *vertex, GX2PixelShader *pixel);

#ifdef __cplusplus
}
#endif
