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
   std::cout << "Usage: glslcompiler.elf [options]\n"
             << "  -vs <file>  Compile a vertex shader\n"
             << "  -ps <file>  Compile a pixel shader\n"
             << "  -o <file>   Write a GFD .gsh file\n"
             << "  -v          Print R600 disassembly\n"
             << "  --allow-uniform-block-fallback\n"
             << "              Move loose uniforms into a uniform block when needed\n";
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
   bool allow_uniform_block_fallback = false;
   std::string output_path;
   std::vector<std::pair<std::string, std::string>> inputs;

   for (int i = 1; i < argc; ++i) {
      if (!strcmp(argv[i], "-v")) {
         verbose = true;
      } else if (!strcmp(argv[i], "--allow-uniform-block-fallback")) {
         allow_uniform_block_fallback = true;
      } else if ((!strcmp(argv[i], "-vs") || !strcmp(argv[i], "-ps")) &&
                 i + 1 < argc) {
         const std::string type = argv[i];
         inputs.emplace_back(type, argv[i + 1]);
         ++i;
      } else if (!strcmp(argv[i], "-o") && i + 1 < argc) {
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

   InitGLSLCompiler();
   unsigned flag_bits = GLSL_COMPILER_FLAG_NONE;
   if (verbose)
      flag_bits |= GLSL_COMPILER_FLAG_GENERATE_DISASSEMBLY;
   if (allow_uniform_block_fallback)
      flag_bits |= GLSL_COMPILER_FLAG_ALLOW_UNIFORM_BLOCK_FALLBACK;
   const GLSL_COMPILER_FLAG flags = static_cast<GLSL_COMPILER_FLAG>(flag_bits);
   std::vector<GX2VertexShader *> vertex_shaders;
   std::vector<GX2PixelShader *> pixel_shaders;
   GFDFile output;
   int result = 0;

   for (const auto &[type, path] : inputs) {
      std::string source;
      if (!ReadFile(path, source)) {
         std::cerr << "Failed to read shader: " << path << '\n';
         result = 1;
         break;
      }

      char diagnostics[8192] = {};
      if (type == "-vs") {
         GX2VertexShader *shader = CompileVertexShader(
            source.c_str(), diagnostics, sizeof(diagnostics), flags);
         if (!shader) {
            std::cerr << path << ": " << diagnostics << '\n';
            result = 1;
            break;
         }
         vertex_shaders.push_back(shader);
         output.vertexShaders.push_back(shader);
      } else {
         GX2PixelShader *shader = CompilePixelShader(
            source.c_str(), diagnostics, sizeof(diagnostics), flags);
         if (!shader) {
            std::cerr << path << ": " << diagnostics << '\n';
            result = 1;
            break;
         }
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
