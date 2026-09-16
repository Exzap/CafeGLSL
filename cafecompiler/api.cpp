#include "cafe_compiler.h"

#include "util/u_memory.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>

static std::mutex compiler_mutex;
static std::unique_ptr<CafeCompiler> compiler;
static unsigned compiler_references;

static void CopyDiagnostics(const std::string &diagnostics, char *output, int output_size)
{
   if (!output || output_size <= 0)
      return;

   const size_t length = std::min(diagnostics.size(), static_cast<size_t>(output_size - 1));
   memcpy(output, diagnostics.data(), length);
   output[length] = '\0';
}

template <typename Shader>
static void FreeShaderCommon(Shader *shader)
{
   if (!shader)
      return;

   if (shader->uniformBlocks) {
      for (uint32_t i = 0; i < shader->uniformBlockCount; ++i)
         free(const_cast<char *>(shader->uniformBlocks[i].name));
   }
   free(shader->uniformBlocks);

   if (shader->uniformVars) {
      for (uint32_t i = 0; i < shader->uniformVarCount; ++i)
         free(const_cast<char *>(shader->uniformVars[i].name));
   }
   free(shader->uniformVars);

   if (shader->samplerVars) {
      for (uint32_t i = 0; i < shader->samplerVarCount; ++i)
         free(const_cast<char *>(shader->samplerVars[i].name));
   }
   free(shader->samplerVars);

   free(shader->initialValues);
   free(shader->loopVars);
   align_free(shader->program);
}

extern "C" {

void InitGLSLCompiler(void)
{
   std::lock_guard<std::mutex> lock(compiler_mutex);
   if (!compiler_references)
      compiler = std::make_unique<CafeCompiler>();
   ++compiler_references;
}

void DestroyGLSLCompiler(void)
{
   std::lock_guard<std::mutex> lock(compiler_mutex);
   if (!compiler_references)
      return;

   if (--compiler_references == 0)
      compiler.reset();
}

const char *GetGLSLCompilerVersion(void)
{
   return "v0.3.0";
}

GX2ShaderMode CompileVertexShader(const char *source, GLSLCompileMode requested_mode,
                                        GX2VertexShader **shader_output, char *info_log,
                                        int info_log_size, GLSL_COMPILER_FLAG flags)
{
   std::lock_guard<std::mutex> lock(compiler_mutex);
   std::string diagnostics;
   GX2ShaderMode requirement = GLSL_SHADER_MODE_ERROR;
   if (shader_output)
      *shader_output = nullptr;

   if (!shader_output)
      diagnostics = "CompileVertexShader requires a shader output pointer";
   else if (!compiler)
      diagnostics = "CafeGLSL is not initialized";
   else if (!compiler->valid())
      diagnostics = compiler->initialization_error();
   else
      requirement = compiler->CompileVertexShader(source, requested_mode, *shader_output, diagnostics, flags);

   CopyDiagnostics(diagnostics, info_log, info_log_size);
   return requirement;
}

GX2ShaderMode CompilePixelShader(const char *source, GLSLCompileMode requested_mode,
                                       GX2PixelShader **shader_output, char *info_log,
                                       int info_log_size, GLSL_COMPILER_FLAG flags)
{
   std::lock_guard<std::mutex> lock(compiler_mutex);
   std::string diagnostics;
   GX2ShaderMode requirement = GLSL_SHADER_MODE_ERROR;
   if (shader_output)
      *shader_output = nullptr;

   if (!shader_output)
      diagnostics = "CompilePixelShader requires a shader output pointer";
   else if (!compiler)
      diagnostics = "CafeGLSL is not initialized";
   else if (!compiler->valid())
      diagnostics = compiler->initialization_error();
   else
      requirement = compiler->CompilePixelShader(source, requested_mode, *shader_output, diagnostics, flags);

   CopyDiagnostics(diagnostics, info_log, info_log_size);
   return requirement;
}

GX2ShaderMode CompileShaderPair(const char *vertex_source, const char *pixel_source,
                                      GLSLCompileMode requested_mode,
                                      GX2VertexShader **vertex_output, GX2PixelShader **pixel_output,
                                      char *info_log, int info_log_size, GLSL_COMPILER_FLAG flags)
{
   std::lock_guard<std::mutex> lock(compiler_mutex);
   std::string diagnostics;
   GX2ShaderMode requirement = GLSL_SHADER_MODE_ERROR;
   if (vertex_output)
      *vertex_output = nullptr;
   if (pixel_output)
      *pixel_output = nullptr;

   if (!vertex_output || !pixel_output)
      diagnostics = "CompileShaderPair requires both shader output pointers";
   else if (!compiler)
      diagnostics = "CafeGLSL is not initialized";
   else if (!compiler->valid())
      diagnostics = compiler->initialization_error();
   else
      requirement = compiler->CompileShaderPair(vertex_source, pixel_source, requested_mode,
                                                *vertex_output, *pixel_output, diagnostics, flags);

   CopyDiagnostics(diagnostics, info_log, info_log_size);
   return requirement;
}

void FreeVertexShader(GX2VertexShader *shader)
{
   if (!shader)
      return;
   FreeShaderCommon(shader);
   if (shader->attribVars) {
      for (uint32_t i = 0; i < shader->attribVarCount; ++i)
         free(const_cast<char *>(shader->attribVars[i].name));
   }
   free(shader->attribVars);
   free(shader);
}

void FreePixelShader(GX2PixelShader *shader)
{
   if (!shader)
      return;
   FreeShaderCommon(shader);
   free(shader);
}

void FreeShaders(GX2VertexShader *vertex, GX2PixelShader *pixel)
{
   FreeVertexShader(vertex);
   FreePixelShader(pixel);
}

} // extern "C"
