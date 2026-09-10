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
   /* Allow loose uniforms to be moved into an implicit uniform block when the
    * shader also uses uniform blocks or exceeds the loose uniform limit.
    * This changes the GX2 uniform upload ABI to GX2Set*UniformBlock.
    * Read the included README.md in the release zip for more info.
    */
   GLSL_COMPILER_FLAG_ALLOW_UNIFORM_BLOCK_FALLBACK = 1 << 1,
} GLSL_COMPILER_FLAG;

void InitGLSLCompiler(void);
void DestroyGLSLCompiler(void);
const char *GetGLSLCompilerVersion(void);

GX2VertexShader *CompileVertexShader(const char *shaderSource,
                                    char *infoLogOut,
                                    int infoLogMaxLength,
                                    GLSL_COMPILER_FLAG flags);
GX2PixelShader *CompilePixelShader(const char *shaderSource,
                                  char *infoLogOut,
                                  int infoLogMaxLength,
                                  GLSL_COMPILER_FLAG flags);
void FreeVertexShader(GX2VertexShader *shader);
void FreePixelShader(GX2PixelShader *shader);

#ifdef __cplusplus
}
#endif
