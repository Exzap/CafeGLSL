#include "CafeGLSLCompiler.h"
#include "gfd.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

static void PrintUsage()
{
   std::cout << "Usage: glslcompiler [options]\n"
             << "  -vs <file>  Compile a vertex shader\n"
             << "  -ps <file>  Compile a pixel shader\n"
             << "  -o <file>   Write a GFD .gsh file\n"
             << "  -v          Print R600 disassembly\n"
             << "  --uniform-mode <auto|register|block>\n"
             << "              auto      use registers when possible, blocks when needed (default)\n"
             << "              register  use registers, fail if blocks are needed\n"
             << "              block     use blocks, moving loose uniforms into a block\n"
             << '\n'
             << "One -vs and one -ps compile together with a shared mode.\n"
             << "Other input combinations compile independently.\n";
}

static const char *ShaderModeName(GX2ShaderMode mode)
{
   switch (mode) {
   case GX2_SHADER_MODE_UNIFORM_REGISTER:
      return "GX2_SHADER_MODE_UNIFORM_REGISTER";
   case GX2_SHADER_MODE_UNIFORM_BLOCK:
      return "GX2_SHADER_MODE_UNIFORM_BLOCK";
   case GX2_SHADER_MODE_GEOMETRY_SHADER:
      return "GX2_SHADER_MODE_GEOMETRY_SHADER";
   case GX2_SHADER_MODE_COMPUTE_SHADER:
      return "GX2_SHADER_MODE_COMPUTE_SHADER";
   default:
      return "GX2_SHADER_MODE_INVALID";
   }
}

static bool ReadFile(const std::string &path, std::string &contents)
{
   std::ifstream input(path, std::ios::binary);
   if (!input)
      return false;
   contents.assign(std::istreambuf_iterator<char>(input),
                   std::istreambuf_iterator<char>());
   return true;
}

int main(int argc, char **argv)
{
   bool verbose = false;
   bool output_specified = false;
   GLSLCompileMode requested_mode = GLSL_COMPILE_AUTO;
   std::string output_path;
   std::vector<std::pair<std::string, std::string>> inputs;

   for (int i = 1; i < argc; ++i) {
      if (!strcmp(argv[i], "-v")) {
         verbose = true;
      } else if (!strcmp(argv[i], "--uniform-mode") && i + 1 < argc) {
         const char *value = argv[++i];
         if (!strcmp(value, "auto"))
            requested_mode = GLSL_COMPILE_AUTO;
         else if (!strcmp(value, "register"))
            requested_mode = GLSL_COMPILE_REGISTER;
         else if (!strcmp(value, "block"))
            requested_mode = GLSL_COMPILE_BLOCK;
         else {
            std::cerr << "Invalid uniform mode: " << value << '\n';
            return 1;
         }
      } else if ((!strcmp(argv[i], "-vs") || !strcmp(argv[i], "-ps")) &&
                 i + 1 < argc) {
         const std::string type = argv[i];
         inputs.emplace_back(type, argv[i + 1]);
         ++i;
      } else if (!strcmp(argv[i], "-o") && i + 1 < argc) {
         if (output_specified) {
            std::cerr << "Error: -o may only be specified once.\n";
            return 1;
         }
         output_specified = true;
         output_path = argv[++i];
      } else {
         PrintUsage();
         return 1;
      }
   }

   if (inputs.empty()) {
      PrintUsage();
      return 1;
   }

   const bool compile_pair = inputs.size() == 2 && inputs[0].first != inputs[1].first;
   InitGLSLCompiler();
   unsigned flag_bits = GLSL_COMPILER_FLAG_NONE;
   if (verbose)
      flag_bits |= GLSL_COMPILER_FLAG_GENERATE_DISASSEMBLY;
   const GLSL_COMPILER_FLAG flags = static_cast<GLSL_COMPILER_FLAG>(flag_bits);
   std::vector<GX2VertexShader *> vertex_shaders;
   std::vector<GX2PixelShader *> pixel_shaders;
   GFDFile output;
   int result = 0;

   if (compile_pair) {
      const auto &vertex_path = inputs[inputs[0].first == "-vs" ? 0 : 1].second;
      const auto &pixel_path = inputs[inputs[0].first == "-ps" ? 0 : 1].second;
      std::string vertex_source, pixel_source;
      if (!ReadFile(vertex_path, vertex_source)) {
         std::cerr << "Failed to read shader: " << vertex_path << '\n';
         result = 1;
      } else if (!ReadFile(pixel_path, pixel_source)) {
         std::cerr << "Failed to read shader: " << pixel_path << '\n';
         result = 1;
      } else {
         GX2VertexShader *vertex = nullptr;
         GX2PixelShader *pixel = nullptr;
         char diagnostics[8192] = {};
         const GX2ShaderMode mode = CompileShaderPair(vertex_source.c_str(), pixel_source.c_str(), requested_mode,
                                 &vertex, &pixel, diagnostics, sizeof(diagnostics), flags);
         if (mode == static_cast<GX2ShaderMode>(-1)) {
            std::cerr << vertex_path << " + " << pixel_path << ": " << diagnostics << '\n';
            result = 1;
         } else {
            std::cout << ShaderModeName(mode) << '\n';
            if (diagnostics[0])
               std::cerr << diagnostics;
            vertex_shaders.push_back(vertex);
            pixel_shaders.push_back(pixel);
            output.vertexShaders.push_back(vertex);
            output.pixelShaders.push_back(pixel);
         }
      }
   }

   for (const auto &[type, path] : inputs) {
      if (compile_pair)
         break;
      std::string source;
      if (!ReadFile(path, source)) {
         std::cerr << "Failed to read shader: " << path << '\n';
         result = 1;
         break;
      }

      char diagnostics[8192] = {};
      if (type == "-vs") {
         GX2VertexShader *shader = nullptr;
         const GX2ShaderMode mode = CompileVertexShader(source.c_str(), requested_mode, &shader,
                                   diagnostics, sizeof(diagnostics), flags);
         if (mode == static_cast<GX2ShaderMode>(-1)) {
            std::cerr << path << ": " << diagnostics << '\n';
            result = 1;
            break;
         }
         std::cout << ShaderModeName(mode) << '\n';
         vertex_shaders.push_back(shader);
         output.vertexShaders.push_back(shader);
      } else {
         GX2PixelShader *shader = nullptr;
         const GX2ShaderMode mode = CompilePixelShader(source.c_str(), requested_mode, &shader,
                                  diagnostics, sizeof(diagnostics), flags);
         if (mode == static_cast<GX2ShaderMode>(-1)) {
            std::cerr << path << ": " << diagnostics << '\n';
            result = 1;
            break;
         }
         std::cout << ShaderModeName(mode) << '\n';
         pixel_shaders.push_back(shader);
         output.pixelShaders.push_back(shader);
      }

      if (diagnostics[0])
         std::cerr << path << ": " << diagnostics << '\n';
   }

   if (!result && !output_path.empty() && !WriteGFD(output, output_path)) {
      std::cerr << "Failed to write GFD file: " << output_path << '\n';
      result = 1;
   }

   for (GX2VertexShader *shader : vertex_shaders)
      FreeVertexShader(shader);
   for (GX2PixelShader *shader : pixel_shaders)
      FreePixelShader(shader);
   DestroyGLSLCompiler();
   return result;
}
