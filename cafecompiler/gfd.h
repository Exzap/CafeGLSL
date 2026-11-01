#pragma once

#include "CafeGLSLCompiler.h"

#include <string>
#include <vector>

struct GFDFile {
   std::vector<const GX2VertexShader *> vertexShaders;
   std::vector<const GX2PixelShader *> pixelShaders;
   std::vector<const GX2ComputeShader *> computeShaders;
};

bool WriteGFD(const GFDFile &file, const std::string &path, bool align_blocks = false);
