#pragma once

#include "CafeGLSLCompiler.h"

#include <string>

struct gl_context;
struct gl_shader_program;
struct gl_program;
struct nir_shader;
struct nir_shader_compiler_options;
struct r600_context;
struct r600_isa;
struct r600_pipe_shader;
struct r600_screen;

class CafeCompiler {
public:
   CafeCompiler();
   ~CafeCompiler();

   CafeCompiler(const CafeCompiler &) = delete;
   CafeCompiler &operator=(const CafeCompiler &) = delete;

   bool valid() const { return m_valid; }
   const std::string &initialization_error() const { return m_initialization_error; }

   GX2ShaderMode CompileVertexShader(const char *source, GLSLCompileMode requested_mode,
                                           GX2VertexShader *&shader_output, std::string &diagnostics,
                                           GLSL_COMPILER_FLAG flags);
   GX2ShaderMode CompilePixelShader(const char *source, GLSLCompileMode requested_mode,
                                          GX2PixelShader *&shader_output, std::string &diagnostics,
                                          GLSL_COMPILER_FLAG flags);
   GX2ShaderMode CompileShaderPair(const char *vertex_source, const char *pixel_source,
                                         GLSLCompileMode requested_mode, GX2VertexShader *&vertex_output,
                                         GX2PixelShader *&pixel_output,
                                         std::string &diagnostics, GLSL_COMPILER_FLAG flags);

private:
   struct CompileState;

   bool InitializeContext();
   bool CompileFrontend(const char *source,
                         unsigned shader_type,
                         CompileState &state,
                         std::string &diagnostics);
   bool SelectMode(GLSLCompileMode requested_mode, CompileState &first,
                   CompileState *second, std::string &diagnostics);
   bool PrepareNir(CompileState &state, std::string &diagnostics);
   bool CompileR600(CompileState &state, std::string &diagnostics);
   GX2VertexShader *CreateVertexShader(CompileState &state,
                                       std::string &diagnostics,
                                       GLSL_COMPILER_FLAG flags);
   GX2PixelShader *CreatePixelShader(CompileState &state,
                                     std::string &diagnostics,
                                     GLSL_COMPILER_FLAG flags);

   gl_context *m_ctx = nullptr;
   nir_shader_compiler_options *m_nir_options = nullptr;
   r600_screen *m_screen = nullptr;
   r600_context *m_rctx = nullptr;
   r600_isa *m_isa = nullptr;
   bool m_builtins_initialized = false;
   bool m_valid = false;
   std::string m_initialization_error;
};
