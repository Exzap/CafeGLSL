#include "CafeGLSLCompiler.h"

#include <cstdio>
#include <cstring>

#define CHECK(condition)                                                        \
   do {                                                                         \
      if (!(condition)) {                                                       \
         fprintf(stderr, "Check failed at %s:%d: %s\n",                       \
                 __FILE__, __LINE__, #condition);                               \
         return 1;                                                              \
      }                                                                          \
   } while (false)

constexpr uint32_t kCfTex = 1;
constexpr uint32_t kCfVtx = 2;
constexpr uint32_t kCfVtxTc = 3;
constexpr uint32_t kCfAlu = 8;
constexpr uint32_t kVFetch = 0;

static int FindUniformBlockIndex(const GX2UniformBlock *blocks,
                                 uint32_t count,
                                 const char *name)
{
   for (uint32_t i = 0; i < count; ++i) {
      if (!strcmp(blocks[i].name, name))
         return static_cast<int>(i);
   }
   return -1;
}

static int FindUniformBlock(const GX2UniformBlock *blocks, uint32_t count, const char *name)
{
   const int index = FindUniformBlockIndex(blocks, count, name);
   return index < 0 ? -1 : static_cast<int>(blocks[index].offset);
}

static int FindSampler(const GX2SamplerVar *samplers, uint32_t count, const char *name)
{
   for (uint32_t i = 0; i < count; ++i) {
      if (!strcmp(samplers[i].name, name))
         return static_cast<int>(samplers[i].location);
   }
   return -1;
}

static int FindAttribute(const GX2AttribVar *attributes, uint32_t count, const char *name)
{
   for (uint32_t i = 0; i < count; ++i) {
      if (!strcmp(attributes[i].name, name))
         return static_cast<int>(attributes[i].location);
   }
   return -1;
}

static const GX2AttribVar *FindAttribVar(const GX2AttribVar *attributes,
                                         uint32_t count,
                                         const char *name)
{
   for (uint32_t i = 0; i < count; ++i) {
      if (!strcmp(attributes[i].name, name))
         return &attributes[i];
   }
   return nullptr;
}

static const GX2SamplerVar *FindSamplerVar(const GX2SamplerVar *samplers,
                                           uint32_t count,
                                           const char *name)
{
   for (uint32_t i = 0; i < count; ++i) {
      if (!strcmp(samplers[i].name, name))
         return &samplers[i];
   }
   return nullptr;
}

static const GX2UniformVar *FindUniform(const GX2UniformVar *uniforms,
                                       uint32_t count,
                                       const char *name)
{
   for (uint32_t i = 0; i < count; ++i) {
      if (!strcmp(uniforms[i].name, name))
         return &uniforms[i];
   }
   return nullptr;
}

static bool HasControlFlowOpcode(const void *program, uint32_t size, uint32_t opcode)
{
   const auto *words = static_cast<const uint32_t *>(program);
   const uint32_t word_count = size / sizeof(uint32_t);
   for (uint32_t i = 0; i + 1 < word_count; i += 2) {
      const uint32_t word1 = words[i + 1];
      if (((word1 >> 23) & 0x7f) == opcode)
         return true;
      if (word1 & (1u << 21))
         break;
   }
   return false;
}

static const uint32_t *FindVFetchInTexClause(const void *program,
                                             uint32_t size,
                                             uint32_t resource_id)
{
   const auto *words = static_cast<const uint32_t *>(program);
   const uint32_t word_count = size / sizeof(uint32_t);
   for (uint32_t i = 0; i + 1 < word_count; i += 2) {
      const uint32_t word0 = words[i];
      const uint32_t word1 = words[i + 1];
      const uint32_t opcode = (word1 >> 23) & 0x7f;
      if (opcode == kCfTex) {
         uint32_t count = (word1 >> 10) & 0x7;
         count |= ((word1 >> 19) & 0x1) << 3;
         count++;

         const uint32_t clause_start = word0 * 2;
         for (uint32_t fetch = 0; fetch < count; ++fetch) {
            const uint32_t fetch_word = clause_start + fetch * 4;
            if (fetch_word + 4 > word_count)
               break;
            const uint32_t vtx_word0 = words[fetch_word];
            if ((vtx_word0 & 0x1f) == kVFetch &&
                ((vtx_word0 >> 8) & 0xff) == resource_id)
               return &words[fetch_word];
         }
      }
      if (word1 & (1u << 21))
         break;
   }
   return nullptr;
}

/* VTX_WORD1.USE_CONST_FIELDS */
static bool VFetchUsesConstFields(const uint32_t *fetch)
{
   return (fetch[1] & (1u << 21)) != 0;
}

static bool HasAluConstSource(const void *program,
                              uint32_t size,
                              uint32_t selector,
                              bool relative)
{
   const auto *words = static_cast<const uint32_t *>(program);
   const uint32_t word_count = size / sizeof(uint32_t);
   for (uint32_t i = 0; i + 1 < word_count; i += 2) {
      const uint32_t cf_word0 = words[i];
      const uint32_t cf_word1 = words[i + 1];
      const uint32_t alu_opcode = (cf_word1 >> 26) & 0xf;
      const bool is_alu = alu_opcode >= kCfAlu;
      if (is_alu) {
         const uint32_t count = ((cf_word1 >> 18) & 0x7f) + 1;
         const uint32_t clause_start = (cf_word0 & 0x3fffff) * 2;
         for (uint32_t instruction = 0; instruction < count; ++instruction) {
            const uint32_t alu_word = clause_start + instruction * 2;
            if (alu_word + 1 >= word_count)
               break;
            const uint32_t word0 = words[alu_word];
            const uint32_t selectors[2] = {
               word0 & 0x1ff,
               (word0 >> 13) & 0x1ff,
            };
            const bool relatives[2] = {
               ((word0 >> 9) & 1) != 0,
               ((word0 >> 22) & 1) != 0,
            };
            for (unsigned source = 0; source < 2; ++source) {
               if (selectors[source] == selector &&
                   (!relative || relatives[source]))
                  return true;
            }
         }
      }
      if (!is_alu && (cf_word1 & (1u << 21)))
         break;
   }
   return false;
}

/* Latte ALU source selectors: 0-127 GPRs, 128-191 the two constant-cache banks,
 * 256-511 the ALU constant file.
 */
static bool HasAluSourceInRange(const void *program,
                                uint32_t size,
                                uint32_t first,
                                uint32_t last)
{
   const auto *words = static_cast<const uint32_t *>(program);
   const uint32_t word_count = size / sizeof(uint32_t);
   for (uint32_t i = 0; i + 1 < word_count; i += 2) {
      const uint32_t cf_word0 = words[i];
      const uint32_t cf_word1 = words[i + 1];
      const bool is_alu = ((cf_word1 >> 26) & 0xf) >= kCfAlu;
      if (is_alu) {
         const uint32_t count = ((cf_word1 >> 18) & 0x7f) + 1;
         const uint32_t clause_start = (cf_word0 & 0x3fffff) * 2;
         for (uint32_t instruction = 0; instruction < count; ++instruction) {
            const uint32_t alu_word = clause_start + instruction * 2;
            if (alu_word + 1 >= word_count)
               break;
            const uint32_t word0 = words[alu_word];
            const uint32_t selectors[2] = {
               word0 & 0x1ff,
               (word0 >> 13) & 0x1ff,
            };
            for (unsigned source = 0; source < 2; ++source) {
               if (selectors[source] >= first && selectors[source] <= last)
                  return true;
            }
         }
      }
      if (!is_alu && (cf_word1 & (1u << 21)))
         break;
   }
   return false;
}

static bool ReadsAluConstFile(const void *program, uint32_t size)
{
   return HasAluSourceInRange(program, size, 256, 511);
}

static bool ReadsKcache(const void *program, uint32_t size)
{
   return HasAluSourceInRange(program, size, 128, 191);
}

/* The Latte semantic a vertex export or a pixel input carries. Both tables hold one
 * byte per parameter; the vertex side packs four of them into each register.
 */
static unsigned VertexExportSemantic(const GX2VertexShader *shader, unsigned param)
{
   return (shader->regs.spi_vs_out_id[param / 4] >> ((param % 4) * 8)) & 0xff;
}

static unsigned PixelInputSemantic(const GX2PixelShader *shader, unsigned input)
{
   return shader->regs.spi_ps_input_cntls[input] & 0xff;
}

static bool HasKcacheBank(const void *program, uint32_t size, uint32_t bank)
{
   const auto *words = static_cast<const uint32_t *>(program);
   const uint32_t word_count = size / sizeof(uint32_t);
   for (uint32_t i = 0; i + 1 < word_count; i += 2) {
      const uint32_t word0 = words[i];
      const uint32_t word1 = words[i + 1];
      const bool is_alu = ((word1 >> 26) & 0xf) >= kCfAlu;
      if (is_alu) {
         const uint32_t mode0 = (word0 >> 30) & 0x3;
         const uint32_t mode1 = word1 & 0x3;
         if ((mode0 && ((word0 >> 22) & 0xf) == bank) ||
             (mode1 && ((word0 >> 26) & 0xf) == bank))
            return true;
      }
      if (!is_alu && (word1 & (1u << 21)))
         break;
   }
   return false;
}

/* NSMBU's Mat block, from the .sharcfb layout of actor/cobStart.szs "mtStart".
 *
 * Not covered, for want of a retail example: blockless uniforms (so bank 0 going
 * to the loose uniforms is unverified), member types beyond
 * FLOAT4/FLOAT3/FLOAT/INT, any second block layout, geometry shaders, and
 * uniform-register mode.
 */
#define NSMBU_MAT_BLOCK                                                         \
   "layout(std140) uniform Mat {\n"                                             \
   "   vec4 mat_color0;\n"                                                      \
   "   vec4 mat_color1;\n"                                                      \
   "   vec4 amb_color0;\n"                                                      \
   "   vec4 amb_color1;\n"                                                      \
   "   vec4 tev_color0;\n"                                                      \
   "   vec4 tev_color1;\n"                                                      \
   "   vec4 tev_color2;\n"                                                      \
   "   vec4 konst0;\n"                                                          \
   "   vec4 konst1;\n"                                                          \
   "   vec4 konst2;\n"                                                          \
   "   vec4 konst3;\n"                                                          \
   "   vec4 ind_texmtx0[2];\n"                                                  \
   "   vec4 ind_texmtx1[2];\n"                                                  \
   "   vec4 ind_texmtx2[2];\n"                                                  \
   "   vec4 texmtx0[3];\n"                                                      \
   "   vec4 texmtx1[3];\n"                                                      \
   "   vec4 texmtx2[3];\n"                                                      \
   "   vec4 texmtx3[3];\n"                                                      \
   "   vec4 texmtx4[3];\n"                                                      \
   "   vec4 texmtx5[3];\n"                                                      \
   "   vec4 texmtx6[3];\n"                                                      \
   "   vec4 texmtx7[3];\n"                                                      \
   "};\n"

/* Referencing every member keeps them all in the symbol table. */
#define NSMBU_MAT_SUM                                                           \
   "(mat_color0 + mat_color1 + amb_color0 + amb_color1 +\n"                     \
   " tev_color0 + tev_color1 + tev_color2 +\n"                                  \
   " konst0 + konst1 + konst2 + konst3 +\n"                                     \
   " ind_texmtx0[0] + ind_texmtx1[1] + ind_texmtx2[0] +\n"                      \
   " texmtx0[0] + texmtx1[1] + texmtx2[2] + texmtx3[0] +\n"                     \
   " texmtx4[1] + texmtx5[2] + texmtx6[0] + texmtx7[2])"

int main()
{
   static const char vertex_source[] = R"(
#version 450
layout(location = 0) in vec2 position;
layout(location = 5) in vec2 offset;
layout(location = 0) out vec2 uv;
layout(binding = 4, std140) uniform VertexData {
   vec4 translation;
   vec4 scale;
};
void main() {
   uv = position;
   gl_Position = vec4(position * scale.xy + offset + translation.xy, 0.0, 1.0);
}
)";

   static const char pixel_source[] = R"(
#version 450
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(binding = 2) uniform sampler2D sourceTexture;
layout(binding = 4) uniform sampler2D extraTextures[2];
layout(binding = 9, std140) uniform PixelData {
   vec4 bias;
   vec4 tint;
};
uniform vec4 baseColor;
uniform float exposure;
struct LooseData {
   vec4 color;
   float factor;
};
uniform LooseData looseData;
void main() {
   color = (texture(sourceTexture, uv) + texture(extraTextures[1], uv)) *
           (tint + bias) * exposure + baseColor +
           looseData.color * looseData.factor;
}
)";

   char diagnostics[4096] = {};
   DestroyGLSLCompiler();
   CHECK(!CompileVertexShader(vertex_source,
                              diagnostics,
                              sizeof(diagnostics),
                              GLSL_COMPILER_FLAG_NONE));
   CHECK(strstr(diagnostics, "not initialized"));

   CHECK(!CompileVertexShader(nullptr,
                              diagnostics,
                              sizeof(diagnostics),
                              GLSL_COMPILER_FLAG_NONE));
   InitGLSLCompiler();
   InitGLSLCompiler();

   CHECK(!CompileVertexShader(nullptr,
                              diagnostics,
                              sizeof(diagnostics),
                              GLSL_COMPILER_FLAG_NONE));
   CHECK(strstr(diagnostics, "null"));

   GX2VertexShader *vertex = CompileVertexShader(
      vertex_source, diagnostics, sizeof(diagnostics), GLSL_COMPILER_FLAG_NONE);
   if (!vertex) {
      fprintf(stderr, "Vertex compilation failed: %s\n", diagnostics);
      return 1;
   }
   CHECK(vertex->program && vertex->size);
   CHECK(vertex->mode == GX2_SHADER_MODE_UNIFORM_BLOCK);
   CHECK(vertex->attribVarCount == 2);
   CHECK(vertex->regs.pa_cl_vs_out_cntl == 0);
   /* Identity map, with a hole for every location the shader does not declare, so
    * that an attribute at location N arrives in R(N + 1).
    */
   CHECK(vertex->regs.num_sq_vtx_semantic == 6);
   CHECK(vertex->regs.sq_vtx_semantic[0] == 0);
   CHECK(vertex->regs.sq_vtx_semantic[1] == 0xff);
   CHECK(vertex->regs.sq_vtx_semantic[5] == 5);
   CHECK((vertex->regs.spi_vs_out_id[0] & 0xff) == 0x8a);
   CHECK(FindUniformBlock(vertex->uniformBlocks,
                          vertex->uniformBlockCount,
                          "VertexData") == 4);
   const GX2UniformVar *scale = FindUniform(
      vertex->uniformVars, vertex->uniformVarCount, "scale");
   CHECK(scale && scale->offset == 4);

   GX2PixelShader *pixel = CompilePixelShader(
      pixel_source, diagnostics, sizeof(diagnostics), GLSL_COMPILER_FLAG_NONE);
   if (!pixel) {
      fprintf(stderr, "Pixel compilation failed: %s\n", diagnostics);
      return 1;
   }
   CHECK(pixel->program && pixel->size);
   CHECK(pixel->mode == GX2_SHADER_MODE_UNIFORM_BLOCK);
   CHECK(strstr(diagnostics, "warning:") &&
         strstr(diagnostics, "GX2SetPixelUniformBlock"));
   CHECK((pixel->regs.spi_ps_input_cntls[0] & 0xff) == 0x8a);
   CHECK(FindUniformBlock(pixel->uniformBlocks,
                          pixel->uniformBlockCount,
                          "PixelData") == 9);
   CHECK(FindUniformBlock(pixel->uniformBlocks,
                          pixel->uniformBlockCount,
                          "__cafe_loose_uniforms") == 0);
   CHECK(pixel->uniformBlockCount == 2);
   CHECK(!HasAluConstSource(pixel->program, pixel->size, 257, false));
   CHECK(HasKcacheBank(pixel->program, pixel->size, 9));
   CHECK(HasKcacheBank(pixel->program, pixel->size, 0));
   CHECK(FindSampler(pixel->samplerVars,
                     pixel->samplerVarCount,
                     "sourceTexture") == 2);
   CHECK(FindSampler(pixel->samplerVars,
                     pixel->samplerVarCount,
                     "extraTextures") == 4);
   const GX2UniformVar *tint = FindUniform(
      pixel->uniformVars, pixel->uniformVarCount, "tint");
   const GX2UniformVar *exposure = FindUniform(
      pixel->uniformVars, pixel->uniformVarCount, "exposure");
   const GX2UniformVar *loose_color = FindUniform(
      pixel->uniformVars, pixel->uniformVarCount, "looseData.color");
   const GX2UniformVar *loose_factor = FindUniform(
      pixel->uniformVars, pixel->uniformVarCount, "looseData.factor");
   const int pixel_loose_block = FindUniformBlockIndex(
      pixel->uniformBlocks, pixel->uniformBlockCount, "__cafe_loose_uniforms");
   CHECK(tint && tint->offset == 4);
   CHECK(tint->block == 0);
   CHECK(exposure && exposure->offset == 4);
   CHECK(exposure->block == pixel_loose_block);
   CHECK(loose_color && loose_factor);
   CHECK(loose_factor->offset > loose_color->offset);
   CHECK(loose_color->block == pixel_loose_block);
   CHECK(loose_factor->block == pixel_loose_block);

   GX2VertexShader *loose_vertex = CompileVertexShader(
      "#version 450\n"
      "uniform vec4 positions[4];\n"
      "void main() { gl_Position = positions[gl_VertexID & 3]; }\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   CHECK(loose_vertex);
   CHECK(loose_vertex->mode == GX2_SHADER_MODE_UNIFORM_REGISTER);
   CHECK(loose_vertex->uniformBlockCount == 0);
   CHECK(!diagnostics[0]);
   CHECK(HasAluConstSource(loose_vertex->program,
                           loose_vertex->size,
                           256,
                           true));

   GX2VertexShader *last_uniform_block = CompileVertexShader(
      "#version 450\n"
      "layout(binding = 15, std140) uniform LastBlock { vec4 position; };\n"
      "void main() { gl_Position = position; }\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   CHECK(last_uniform_block);
   CHECK(last_uniform_block->mode == GX2_SHADER_MODE_UNIFORM_BLOCK);
   CHECK(FindUniformBlock(last_uniform_block->uniformBlocks,
                          last_uniform_block->uniformBlockCount,
                          "LastBlock") == 15);
   CHECK(HasKcacheBank(last_uniform_block->program,
                       last_uniform_block->size,
                       15));

   GX2PixelShader *sampler_aggregate = CompilePixelShader(
      "#version 450\n"
      "struct Material { sampler2D texture; };\n"
      "uniform Material material;\n"
      "layout(location = 0) out vec4 color;\n"
      "void main() { color = texture(material.texture, vec2(0.0)); }\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   CHECK(!sampler_aggregate);
   CHECK(strstr(diagnostics, "structures"));

   /* An attribute that linking drops must not pull the ones behind it down a slot:
    * the host still binds tail by its declared location, so it has to stay in R3.
    */
   GX2VertexShader *dropped_attribute = CompileVertexShader(
      "#version 450\n"
      "layout(location = 0) in vec4 head;\n"
      "layout(location = 1) in vec4 unused;\n"
      "layout(location = 2) in vec4 tail;\n"
      "layout(location = 0) out vec4 color;\n"
      "void main() { gl_Position = head; color = tail; }\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   CHECK(dropped_attribute);
   CHECK(dropped_attribute->attribVarCount == 2);
   CHECK(FindAttribute(dropped_attribute->attribVars,
                       dropped_attribute->attribVarCount,
                       "tail") == 2);
   CHECK(dropped_attribute->regs.num_sq_vtx_semantic == 3);
   CHECK(dropped_attribute->regs.sq_vtx_semantic[0] == 0);
   CHECK(dropped_attribute->regs.sq_vtx_semantic[1] == 0xff);
   CHECK(dropped_attribute->regs.sq_vtx_semantic[2] == 2);
   /* NUM_GPRS: R0 plus R1-R3 for locations 0-2. */
   CHECK((dropped_attribute->regs.sq_pgm_resources_vs & 0xff) >= 4);

   GX2VertexShader *double_attribute = CompileVertexShader(
      "#version 450\n"
      "layout(location = 0) in vec4 head;\n"
      "layout(location = 2) in dvec4 wide;\n"
      "layout(location = 0) out vec4 color;\n"
      "void main() { gl_Position = head; color = vec4(wide); }\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   CHECK(!double_attribute);
   CHECK(strstr(diagnostics, "64-bit"));

   /* A shader must not source constants from both the ALU constant file and a kcache
    * bank. Cemu's decompiler picks one uniform mode per shader and only its REMAPPED
    * mode translates both kinds; a relative constant-file read forces FULL_CFILE,
    * where every constant source is emitted as a uniform register and the kcache reads
    * silently come out wrong. So a shader that mixes loose uniforms with a uniform
    * block has to put the loose ones in a block too.
    */
   GX2VertexShader *indexed_loose_with_block = CompileVertexShader(
      "#version 450\n"
      "uniform vec4 positions[4];\n"
      "layout(binding = 3, std140) uniform Data { vec4 scale; };\n"
      "void main() { gl_Position = positions[gl_VertexID & 3] * scale; }\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   CHECK(indexed_loose_with_block);
   CHECK(!(ReadsAluConstFile(indexed_loose_with_block->program,
                             indexed_loose_with_block->size) &&
           ReadsKcache(indexed_loose_with_block->program,
                       indexed_loose_with_block->size)));

   GX2VertexShader *loose_pair_with_block = CompileVertexShader(
      "#version 450\n"
      "uniform vec4 tint;\n"
      "uniform vec4 positions[4];\n"
      "layout(binding = 3, std140) uniform Data { vec4 scale; };\n"
      "void main() { gl_Position = positions[gl_VertexID & 3] * scale * tint; }\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   CHECK(loose_pair_with_block);
   CHECK(!(ReadsAluConstFile(loose_pair_with_block->program,
                             loose_pair_with_block->size) &&
           ReadsKcache(loose_pair_with_block->program,
                       loose_pair_with_block->size)));

   GX2VertexShader *mixed_uniforms = CompileVertexShader(
      "#version 450\n"
      "layout(binding = 15, std140) uniform LastBlock { vec4 position; };\n"
      "uniform vec4 offset;\n"
      "void main() { gl_Position = position + offset; }\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   CHECK(mixed_uniforms);
   CHECK(mixed_uniforms->mode == GX2_SHADER_MODE_UNIFORM_BLOCK);
   CHECK(mixed_uniforms->uniformBlockCount == 2);
   CHECK(FindUniformBlock(mixed_uniforms->uniformBlocks,
                          mixed_uniforms->uniformBlockCount,
                          "__cafe_loose_uniforms") == 0);
   CHECK(FindUniformBlock(mixed_uniforms->uniformBlocks,
                          mixed_uniforms->uniformBlockCount,
                          "LastBlock") == 15);
   CHECK(HasKcacheBank(mixed_uniforms->program, mixed_uniforms->size, 0));
   CHECK(HasKcacheBank(mixed_uniforms->program, mixed_uniforms->size, 15));
   CHECK(!ReadsAluConstFile(mixed_uniforms->program, mixed_uniforms->size));
   CHECK(strstr(diagnostics, "warning:"));
   CHECK(strstr(diagnostics, "GX2SetVertexUniformBlock"));

   /* Only mixed shaders lose binding 0. */
   CHECK(!CompileVertexShader(
      "#version 450\n"
      "layout(binding = 0, std140) uniform Data { vec4 scale; };\n"
      "uniform vec4 offset;\n"
      "void main() { gl_Position = scale + offset; }\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE));
   CHECK(strstr(diagnostics, "binding 0 is reserved"));

   /* Deliberately an error rather than a silent move into a block. */
   CHECK(!CompileVertexShader(
      "#version 450\n"
      "uniform vec4 positions[300];\n"
      "void main() { gl_Position = positions[gl_VertexID & 255]; }\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE));
   CHECK(strstr(diagnostics, "ALU constant file"));

   /* The same array is fine once a block has forced everything into the kcache. */
   GX2VertexShader *oversized_mixed = CompileVertexShader(
      "#version 450\n"
      "layout(binding = 7, std140) uniform Data { vec4 scale; };\n"
      "uniform vec4 positions[300];\n"
      "void main() { gl_Position = positions[gl_VertexID & 255] * scale; }\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   CHECK(oversized_mixed);
   CHECK(oversized_mixed->mode == GX2_SHADER_MODE_UNIFORM_BLOCK);
   CHECK(FindUniformBlock(oversized_mixed->uniformBlocks,
                          oversized_mixed->uniformBlockCount,
                          "__cafe_loose_uniforms") == 0);
   CHECK(oversized_mixed->uniformBlocks[FindUniformBlockIndex(
            oversized_mixed->uniformBlocks,
            oversized_mixed->uniformBlockCount,
            "__cafe_loose_uniforms")].size == 300 * 16);

   GX2VertexShader *vertex_id = CompileVertexShader(
      "#version 450\n"
      "layout(binding = 3, std140) uniform VertexTable { vec4 values[4]; };\n"
      "void main() { gl_Position = values[gl_VertexID & 3]; }\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   CHECK(vertex_id);
   CHECK(FindUniformBlock(vertex_id->uniformBlocks,
                          vertex_id->uniformBlockCount,
                          "VertexTable") == 3);
   CHECK(HasControlFlowOpcode(vertex_id->program, vertex_id->size, kCfTex));
   CHECK(!HasControlFlowOpcode(vertex_id->program, vertex_id->size, kCfVtx));
   CHECK(!HasControlFlowOpcode(vertex_id->program, vertex_id->size, kCfVtxTc));
   const uint32_t *vertex_id_fetch =
      FindVFetchInTexClause(vertex_id->program, vertex_id->size, 0x83);
   CHECK(vertex_id_fetch);
   CHECK(VFetchUsesConstFields(vertex_id_fetch));

   GX2PixelShader *environment_pass = CompilePixelShader(
      "#version 450\n"
      "layout(binding = 0) uniform uf_data { vec2 pixelSize; uint isVertical; };\n"
      "layout(location = 0) out vec2 color;\n"
      "const int Quality = 5;\n"
      "const float Weights[5] = float[](0.227027, 0.1945946, 0.1216216,\n"
      "                                  0.054054, 0.016216);\n"
      "void main() {\n"
      "   color = pixelSize * Weights[0] + vec2(float(isVertical));\n"
      "   for (int i = 1; i < Quality; ++i)\n"
      "      color += pixelSize * Weights[i];\n"
      "}\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   CHECK(environment_pass);
   CHECK(FindUniformBlock(environment_pass->uniformBlocks,
                          environment_pass->uniformBlockCount,
                          "uf_data") == 0);

   GX2VertexShader *dynamic_block = CompileVertexShader(
      "#version 450\n"
      "layout(binding = 0, std140) uniform Block { vec4 value; } blocks[2];\n"
      "void main() { gl_Position = blocks[gl_VertexID & 1].value; }\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   CHECK(!dynamic_block);
   CHECK(strstr(diagnostics, "UBO indexing"));

   /* Test shaders taken from abood's RIO-Tests, with permission.
    * https://github.com/aboood40091/RIO-Tests
    */
   static const char rio_primitive_vertex[] = R"(
#version 330 core

uniform vec4 wvp[4];
uniform vec4 user[3];
uniform vec4 color0;
uniform vec4 color1;

in vec3 Vertex;
in vec2 TexCoord0;
in vec4 ColorRate;

out vec4 Color;
out vec2 TexCoord;

void main()
{
    vec4 uwvp[4] = vec4[4](
        vec4(
            wvp[0][0] * user[0][0] + wvp[0][1] * user[1][0] + wvp[0][2] * user[2][0],
            wvp[0][0] * user[0][1] + wvp[0][1] * user[1][1] + wvp[0][2] * user[2][1],
            wvp[0][0] * user[0][2] + wvp[0][1] * user[1][2] + wvp[0][2] * user[2][2],
            wvp[0][0] * user[0][3] + wvp[0][1] * user[1][3] + wvp[0][2] * user[2][3] + wvp[0][3]
        ),
        vec4(
            wvp[1][0] * user[0][0] + wvp[1][1] * user[1][0] + wvp[1][2] * user[2][0],
            wvp[1][0] * user[0][1] + wvp[1][1] * user[1][1] + wvp[1][2] * user[2][1],
            wvp[1][0] * user[0][2] + wvp[1][1] * user[1][2] + wvp[1][2] * user[2][2],
            wvp[1][0] * user[0][3] + wvp[1][1] * user[1][3] + wvp[1][2] * user[2][3] + wvp[1][3]
        ),
        vec4(
            wvp[2][0] * user[0][0] + wvp[2][1] * user[1][0] + wvp[2][2] * user[2][0],
            wvp[2][0] * user[0][1] + wvp[2][1] * user[1][1] + wvp[2][2] * user[2][1],
            wvp[2][0] * user[0][2] + wvp[2][1] * user[1][2] + wvp[2][2] * user[2][2],
            wvp[2][0] * user[0][3] + wvp[2][1] * user[1][3] + wvp[2][2] * user[2][3] + wvp[2][3]
        ),
        vec4(
            wvp[3][0] * user[0][0] + wvp[3][1] * user[1][0] + wvp[3][2] * user[2][0],
            wvp[3][0] * user[0][1] + wvp[3][1] * user[1][1] + wvp[3][2] * user[2][1],
            wvp[3][0] * user[0][2] + wvp[3][1] * user[1][2] + wvp[3][2] * user[2][2],
            wvp[3][0] * user[0][3] + wvp[3][1] * user[1][3] + wvp[3][2] * user[2][3] + wvp[3][3]
        )
    );
    vec4 pos = vec4(Vertex, 1);

    gl_Position = vec4(dot(uwvp[0], pos),
                       dot(uwvp[1], pos),
                       dot(uwvp[2], pos),
                       dot(uwvp[3], pos));

    Color = color0 * (1.0 - ColorRate.r) + color1 * ColorRate.r;
    TexCoord = TexCoord0;
}
)";

   static const char rio_primitive_pixel[] = R"(
#version 330 core

uniform sampler2D texture0;
uniform float rate;

in vec4 Color;
in vec2 TexCoord;

out vec4 FragColor;

void main()
{
    vec4 color = texture(texture0, TexCoord);
    FragColor.r = Color.r * (color.r * rate + (1 - rate));
    FragColor.g = Color.g * (color.g * rate + (1 - rate));
    FragColor.b = Color.b * (color.b * rate + (1 - rate));
    FragColor.a = Color.a * (color.a * rate + (1 - rate));
}
)";

   GX2VertexShader *rio_primitive_vs = CompileVertexShader(
      rio_primitive_vertex, diagnostics, sizeof(diagnostics), GLSL_COMPILER_FLAG_NONE);
   if (!rio_primitive_vs) {
      fprintf(stderr, "RIO primitive_renderer.vert failed: %s\n", diagnostics);
      return 1;
   }
   CHECK(rio_primitive_vs->mode == GX2_SHADER_MODE_UNIFORM_REGISTER);
   CHECK(rio_primitive_vs->uniformBlockCount == 0);
   /* No layout qualifiers, so these have to come out in declaration order. */
   CHECK(rio_primitive_vs->attribVarCount == 3);
   CHECK(FindAttribute(rio_primitive_vs->attribVars,
                       rio_primitive_vs->attribVarCount, "Vertex") == 0);
   CHECK(FindAttribute(rio_primitive_vs->attribVars,
                       rio_primitive_vs->attribVarCount, "TexCoord0") == 1);
   CHECK(FindAttribute(rio_primitive_vs->attribVars,
                       rio_primitive_vs->attribVarCount, "ColorRate") == 2);
   CHECK(rio_primitive_vs->regs.num_sq_vtx_semantic == 3);
   CHECK(rio_primitive_vs->regs.sq_vtx_semantic[0] == 0);
   CHECK(rio_primitive_vs->regs.sq_vtx_semantic[1] == 1);
   CHECK(rio_primitive_vs->regs.sq_vtx_semantic[2] == 2);
   /* rio::PrimitiveRenderer uploads these as raw vec4 runs, so they pack tightly. */
   const GX2UniformVar *rio_wvp = FindUniform(
      rio_primitive_vs->uniformVars, rio_primitive_vs->uniformVarCount, "wvp");
   const GX2UniformVar *rio_user = FindUniform(
      rio_primitive_vs->uniformVars, rio_primitive_vs->uniformVarCount, "user");
   const GX2UniformVar *rio_color0 = FindUniform(
      rio_primitive_vs->uniformVars, rio_primitive_vs->uniformVarCount, "color0");
   const GX2UniformVar *rio_color1 = FindUniform(
      rio_primitive_vs->uniformVars, rio_primitive_vs->uniformVarCount, "color1");
   CHECK(rio_wvp && rio_wvp->count == 4 && rio_wvp->offset == 0);
   CHECK(rio_wvp->type == GX2_SHADER_VAR_TYPE_FLOAT4 && rio_wvp->block == -1);
   CHECK(rio_user && rio_user->count == 3 && rio_user->offset == 16);
   CHECK(rio_color0 && rio_color0->count == 1 && rio_color0->offset == 28);
   CHECK(rio_color1 && rio_color1->count == 1 && rio_color1->offset == 32);

   GX2PixelShader *rio_primitive_ps = CompilePixelShader(
      rio_primitive_pixel, diagnostics, sizeof(diagnostics), GLSL_COMPILER_FLAG_NONE);
   if (!rio_primitive_ps) {
      fprintf(stderr, "RIO primitive_renderer.frag failed: %s\n", diagnostics);
      return 1;
   }
   CHECK(rio_primitive_ps->mode == GX2_SHADER_MODE_UNIFORM_REGISTER);
   CHECK(FindSampler(rio_primitive_ps->samplerVars,
                     rio_primitive_ps->samplerVarCount, "texture0") == 0);
   const GX2UniformVar *rio_rate = FindUniform(
      rio_primitive_ps->uniformVars, rio_primitive_ps->uniformVarCount, "rate");
   CHECK(rio_rate && rio_rate->offset == 0 && rio_rate->block == -1);
   /* The two separate compiles have to agree on which semantic carries what. */
   CHECK(rio_primitive_vs->regs.num_spi_vs_out_id == 1);
   CHECK(rio_primitive_ps->regs.num_spi_ps_input_cntl == 2);
   CHECK(VertexExportSemantic(rio_primitive_vs, 0) ==
         PixelInputSemantic(rio_primitive_ps, 0));
   CHECK(VertexExportSemantic(rio_primitive_vs, 1) ==
         PixelInputSemantic(rio_primitive_ps, 1));

   /* RIO-Tests 05: camera matrix as four loose vec4 rows, applied with dot(). */
   GX2VertexShader *rio_mvp_vs = CompileVertexShader(
      "#version 330 core\n"
      "\n"
      "uniform vec4 mvp[4];\n"
      "\n"
      "layout(location = 0) in vec3 v_inPos;\n"
      "layout(location = 1) in vec2 v_inTexCoord;\n"
      "\n"
      "out vec2 TexCoord;\n"
      "\n"
      "void main()\n"
      "{\n"
      "    TexCoord = v_inTexCoord;\n"
      "\n"
      "    vec4 pos = vec4(v_inPos, 1.0f);\n"
      "    gl_Position = vec4(dot(mvp[0], pos), dot(mvp[1], pos), dot(mvp[2], pos), dot(mvp[3], pos));\n"
      "}\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   if (!rio_mvp_vs) {
      fprintf(stderr, "RIO-Tests 05 test_shader.vert failed: %s\n", diagnostics);
      return 1;
   }
   CHECK(rio_mvp_vs->mode == GX2_SHADER_MODE_UNIFORM_REGISTER);
   CHECK(rio_mvp_vs->uniformBlockCount == 0);
   CHECK(FindAttribute(rio_mvp_vs->attribVars,
                       rio_mvp_vs->attribVarCount, "v_inPos") == 0);
   CHECK(FindAttribute(rio_mvp_vs->attribVars,
                       rio_mvp_vs->attribVarCount, "v_inTexCoord") == 1);
   const GX2UniformVar *rio_mvp = FindUniform(
      rio_mvp_vs->uniformVars, rio_mvp_vs->uniformVarCount, "mvp");
   CHECK(rio_mvp && rio_mvp->count == 4 && rio_mvp->offset == 0);
   CHECK(rio_mvp->block == -1);

   /* Sampler-only uniforms touch neither file, so uniform-block mode stays free. */
   GX2PixelShader *rio_mix_ps = CompilePixelShader(
      "#version 330 core\n"
      "\n"
      "uniform sampler2D texture0;\n"
      "uniform sampler2D texture1;\n"
      "\n"
      "in vec2 TexCoord;\n"
      "\n"
      "out vec4 o_FragColor;\n"
      "\n"
      "void main()\n"
      "{\n"
      "    o_FragColor = mix(texture(texture0, TexCoord), texture(texture1, TexCoord), 0.2f);\n"
      "}\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   if (!rio_mix_ps) {
      fprintf(stderr, "RIO-Tests 05 test_shader.frag failed: %s\n", diagnostics);
      return 1;
   }
   CHECK(rio_mix_ps->mode == GX2_SHADER_MODE_UNIFORM_BLOCK);
   CHECK(rio_mix_ps->uniformVarCount == 0);
   CHECK(FindSampler(rio_mix_ps->samplerVars,
                     rio_mix_ps->samplerVarCount, "texture0") == 0);
   CHECK(FindSampler(rio_mix_ps->samplerVars,
                     rio_mix_ps->samplerVarCount, "texture1") == 1);
   CHECK(rio_mix_ps->regs.num_spi_ps_input_cntl == 1);
   CHECK(VertexExportSemantic(rio_mvp_vs, 0) == PixelInputSemantic(rio_mix_ps, 0));

   /* setUniform writes a vec4 at a time, so each vec3 takes a full slot. */
   GX2PixelShader *rio_light_ps = CompilePixelShader(
      "#version 330 core\n"
      "\n"
      "uniform vec3 lightColor;\n"
      "uniform vec3 lightPos;\n"
      "uniform vec3 viewPos;\n"
      "\n"
      "uniform sampler2D texture0;\n"
      "uniform sampler2D texture1;\n"
      "\n"
      "in vec3 FragPos;\n"
      "in vec2 TexCoord;\n"
      "in vec3 Normal;\n"
      "\n"
      "out vec4 o_FragColor;\n"
      "\n"
      "void main()\n"
      "{\n"
      "    vec4 texColor = mix(texture(texture0, TexCoord), texture(texture1, TexCoord), 0.2f);\n"
      "    vec3 ambient = 0.4f * lightColor;\n"
      "    vec3 lightDir = normalize(lightPos - FragPos);\n"
      "    vec3 diffuse = max(dot(Normal, lightDir), 0.0f) * lightColor;\n"
      "    vec3 viewDir = normalize(viewPos - FragPos);\n"
      "    vec3 reflectDir = reflect(-lightDir, Normal);\n"
      "    vec3 specular = 0.4f * pow(max(dot(viewDir, reflectDir), 0.0f), 32) * lightColor;\n"
      "    o_FragColor = vec4((ambient + diffuse + specular) * texColor.rgb, texColor.a);\n"
      "}\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   if (!rio_light_ps) {
      fprintf(stderr, "RIO-Tests 07 test_shader.frag failed: %s\n", diagnostics);
      return 1;
   }
   CHECK(rio_light_ps->mode == GX2_SHADER_MODE_UNIFORM_REGISTER);
   const GX2UniformVar *rio_light_color = FindUniform(
      rio_light_ps->uniformVars, rio_light_ps->uniformVarCount, "lightColor");
   const GX2UniformVar *rio_light_pos = FindUniform(
      rio_light_ps->uniformVars, rio_light_ps->uniformVarCount, "lightPos");
   const GX2UniformVar *rio_view_pos = FindUniform(
      rio_light_ps->uniformVars, rio_light_ps->uniformVarCount, "viewPos");
   CHECK(rio_light_color && rio_light_color->offset == 0);
   CHECK(rio_light_color->type == GX2_SHADER_VAR_TYPE_FLOAT3);
   CHECK(rio_light_pos && rio_light_pos->offset == 4);
   CHECK(rio_view_pos && rio_view_pos->offset == 8);
   CHECK(FindSampler(rio_light_ps->samplerVars,
                     rio_light_ps->samplerVarCount, "texture0") == 0);
   CHECK(FindSampler(rio_light_ps->samplerVars,
                     rio_light_ps->samplerVarCount, "texture1") == 1);
   CHECK(rio_light_ps->regs.num_spi_ps_input_cntl == 3);

   /* Same three varyings in opposite orders, one surviving each side, so the name is
    * all the two compiles have left in common.
    */
   GX2VertexShader *rio_named_varying_vs = CompileVertexShader(
      "#version 330 core\n"
      "layout(location = 0) in vec3 v_inPos;\n"
      "out vec3 FragPos;\n"
      "out vec2 TexCoord;\n"
      "out vec3 Normal;\n"
      "void main() { Normal = v_inPos.zyx; gl_Position = vec4(v_inPos, 1.0); }\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   GX2PixelShader *rio_named_varying_ps = CompilePixelShader(
      "#version 330 core\n"
      "in vec3 Normal;\n"
      "in vec2 TexCoord;\n"
      "in vec3 FragPos;\n"
      "out vec4 o_FragColor;\n"
      "void main() { o_FragColor = vec4(Normal, 1.0); }\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   CHECK(rio_named_varying_vs && rio_named_varying_ps);
   CHECK(rio_named_varying_vs->regs.num_spi_vs_out_id == 1);
   CHECK(rio_named_varying_ps->regs.num_spi_ps_input_cntl == 1);
   CHECK(VertexExportSemantic(rio_named_varying_vs, 0) ==
         PixelInputSemantic(rio_named_varying_ps, 0));

   /* RIO declares its blocks without a binding, the way GLSL 150 forces. A lone
    * block lands in bank 1.
    */
   GX2VertexShader *rio_implicit_block_vs = CompileVertexShader(
      "#version 330 core\n"
      "\n"
      "layout(std140)\n"
      "uniform cViewBlock\n"
      "{\n"
      "    vec3 viewPos;\n"
      "    vec4 viewProj[4];\n"
      "};\n"
      "\n"
      "layout(location = 0) in vec3 v_inPos;\n"
      "\n"
      "void main()\n"
      "{\n"
      "    vec4 wpos = vec4(v_inPos + viewPos, 1.0);\n"
      "    gl_Position = vec4(dot(viewProj[0], wpos), dot(viewProj[1], wpos),\n"
      "                       dot(viewProj[2], wpos), dot(viewProj[3], wpos));\n"
      "}\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   if (!rio_implicit_block_vs) {
      fprintf(stderr, "RIO-Tests 08 without a binding failed: %s\n", diagnostics);
      return 1;
   }
   CHECK(rio_implicit_block_vs->mode == GX2_SHADER_MODE_UNIFORM_BLOCK);
   CHECK(FindUniformBlock(rio_implicit_block_vs->uniformBlocks,
                          rio_implicit_block_vs->uniformBlockCount,
                          "cViewBlock") == 1);
   const GX2UniformVar *rio_implicit_view_proj = FindUniform(
      rio_implicit_block_vs->uniformVars,
      rio_implicit_block_vs->uniformVarCount, "viewProj");
   CHECK(rio_implicit_view_proj && rio_implicit_view_proj->offset == 4);
   CHECK(rio_implicit_view_proj->count == 4 && rio_implicit_view_proj->block == 0);

   /* Adding the binding is not enough: #version 330 core has no binding qualifier
    * without ARB_shading_language_420pack.
    */
   GX2VertexShader *rio_unqualified_binding_vs = CompileVertexShader(
      "#version 330 core\n"
      "layout(binding = 0, std140) uniform cViewBlock { vec4 viewProj[4]; };\n"
      "void main() { gl_Position = viewProj[0]; }\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   CHECK(!rio_unqualified_binding_vs);
   CHECK(strstr(diagnostics, "unrecognized layout identifier"));

   /* With the extension, the same block compiles and lands in the bank it names. */
   GX2VertexShader *rio_bound_block_vs = CompileVertexShader(
      "#version 330 core\n"
      "#extension GL_ARB_shading_language_420pack : require\n"
      "\n"
      "layout(binding = 0, std140)\n"
      "uniform cViewBlock\n"
      "{\n"
      "    vec3 viewPos;\n"
      "    vec4 viewProj[4];\n"
      "};\n"
      "\n"
      "layout(location = 0) in vec3 v_inPos;\n"
      "\n"
      "void main()\n"
      "{\n"
      "    vec4 wpos = vec4(v_inPos + viewPos, 1.0);\n"
      "    gl_Position = vec4(dot(viewProj[0], wpos), dot(viewProj[1], wpos),\n"
      "                       dot(viewProj[2], wpos), dot(viewProj[3], wpos));\n"
      "}\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   if (!rio_bound_block_vs) {
      fprintf(stderr, "RIO-Tests 08 with an explicit binding failed: %s\n", diagnostics);
      return 1;
   }
   CHECK(rio_bound_block_vs->mode == GX2_SHADER_MODE_UNIFORM_BLOCK);
   CHECK(FindUniformBlock(rio_bound_block_vs->uniformBlocks,
                          rio_bound_block_vs->uniformBlockCount,
                          "cViewBlock") == 0);
   /* std140 rounds the leading vec3 up to a full vec4 before the array starts. */
   const GX2UniformVar *rio_view_proj = FindUniform(
      rio_bound_block_vs->uniformVars, rio_bound_block_vs->uniformVarCount, "viewProj");
   CHECK(rio_view_proj && rio_view_proj->offset == 4);
   CHECK(rio_view_proj->count == 4 && rio_view_proj->block == 0);

   /* A block that names its bank keeps it and the rest fit around it. */
   GX2VertexShader *pinned_block_vs = CompileVertexShader(
      "#version 150\n"
      "#extension GL_ARB_shading_language_420pack : require\n"
      "layout(std140) uniform Mat { vec4 mat_color0; };\n"
      "layout(std140, binding = 0) uniform MdlMtx { vec4 cWorld; };\n"
      "layout(std140) uniform Shp { vec4 cShpMtx; };\n"
      "void main() { gl_Position = mat_color0 + cWorld + cShpMtx; }\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   if (!pinned_block_vs) {
      fprintf(stderr, "Mixed block bindings failed: %s\n", diagnostics);
      return 1;
   }
   CHECK(FindUniformBlock(pinned_block_vs->uniformBlocks,
                          pinned_block_vs->uniformBlockCount, "MdlMtx") == 0);
   CHECK(FindUniformBlock(pinned_block_vs->uniformBlocks,
                          pinned_block_vs->uniformBlockCount, "Shp") == 1);
   CHECK(FindUniformBlock(pinned_block_vs->uniformBlocks,
                          pinned_block_vs->uniformBlockCount, "Mat") == 2);

   /* agl prepends "#version 330" to GX2 shader source, so SDK-era shaders often omit
    * the directive - some of NSMBU's declare std140 blocks with no #version at all,
    * which GLSL 1.10 has no such thing as. Keep the binding explicit here so this test
    * covers the language default independently of implicit UBO binding assignment.
    */
   GX2VertexShader *implicit_version_vs = CompileVertexShader(
      "#extension GL_ARB_shading_language_420pack : require\n"
      "layout(std140, binding = 0) uniform Shp { vec4 shape; };\n"
      "in vec4 aPosition;\n"
      "void main() { gl_Position = aPosition + shape; }\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   if (!implicit_version_vs) {
      fprintf(stderr, "Shader without a #version failed: %s\n", diagnostics);
      return 1;
   }
   CHECK(FindUniformBlock(implicit_version_vs->uniformBlocks,
                          implicit_version_vs->uniformBlockCount, "Shp") == 0);

   /* The preprocessor keeps its own implicit version, so it has to move too - otherwise
    * __VERSION__ and the GL_ARB_* macros it gates describe a different language than the
    * one the shader is then parsed as.
    */
   GX2VertexShader *implicit_version_pp_vs = CompileVertexShader(
      "#if __VERSION__ != 330\n"
      "#error implicit version did not reach the preprocessor\n"
      "#endif\n"
      "void main() { gl_Position = vec4(0.0); }\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   if (!implicit_version_pp_vs) {
      fprintf(stderr, "__VERSION__ without a #version failed: %s\n", diagnostics);
      return 1;
   }

   /* A default, not a force: a shader that names its version still gets it, with all the
    * restrictions that come with it. This is what separates DefaultGLSLVersion from
    * ForceGLSLVersion, so it stays here to catch anyone folding the two together.
    */
   GX2VertexShader *declared_version_vs = CompileVertexShader(
      "#version 110\n"
      "#if __VERSION__ != 110\n"
      "#error declared version was overridden\n"
      "#endif\n"
      "void main() { gl_Position = vec4(0.0); }\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   if (!declared_version_vs) {
      fprintf(stderr, "Declared #version 110 failed: %s\n", diagnostics);
      return 1;
   }
   CHECK(!CompileVertexShader(
      "#version 110\n"
      "layout(std140) uniform Shp { vec4 shape; };\n"
      "void main() { gl_Position = shape; }\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE));

   /* A version-less shader has not asked for core semantics, so attribute/varying stay
    * legal - the SDK-era sources that omit #version are exactly the old-style ones.
    * (Fixed-function attributes like gl_Color are a separate matter: Latte's semantic
    * map only covers the generic slots, so those still get rejected.)
    */
   GX2VertexShader *implicit_version_compat_vs = CompileVertexShader(
      "attribute vec4 aPosition;\n"
      "varying vec4 vColor;\n"
      "void main() { gl_Position = aPosition; vColor = aPosition.yxzw; }\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   if (!implicit_version_compat_vs) {
      fprintf(stderr, "Version-less compat shader failed: %s\n", diagnostics);
      return 1;
   }

   /* NSMBU shaders indicate that the official SDK accepts failed token pastes as adjacency. */
   static const char paste_source[] =
      "#version 150\n"
      "#extension GL_ARB_shading_language_420pack : require\n"
      "#define expand_tex_coord( n ) tex_coord##n##.x\n"
      "#define expand_amb( n ) cAmbColor[##n]\n"
      "layout(std140, binding = 0) uniform Amb { vec4 cAmbColor[4]; };\n"
      "in vec4 tex_coord0;\n"
      "void main() {\n"
      "   gl_Position = vec4(expand_tex_coord( 0 )) + expand_amb( 2 );\n"
      "}\n";

   GX2VertexShader *adjacent_paste_vs = CompileVertexShader(
      paste_source,
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   if (!adjacent_paste_vs) {
      fprintf(stderr, "Adjacent token pasting failed: %s\n", diagnostics);
      return 1;
   }
   CHECK(FindAttribute(adjacent_paste_vs->attribVars,
                       adjacent_paste_vs->attribVarCount, "tex_coord0") >= 0);

   GX2VertexShader *valid_paste_vs = CompileVertexShader(
      "#version 150\n"
      "#define GLUE( a, b ) a##b\n"
      "#define VARIANT 1\n"
      "in vec4 attr_one;\n"
      "in vec4 attr_two;\n"
      "#if VARIANT\n"
      "#define PICK GLUE( attr_, one )\n"
      "#else\n"
      "#define PICK GLUE( attr_, two )\n"
      "#endif\n"
      "void main() { gl_Position = PICK; }\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   if (!valid_paste_vs) {
      fprintf(stderr, "Identifier token pasting failed: %s\n", diagnostics);
      return 1;
   }
   CHECK(FindAttribute(valid_paste_vs->attribVars,
                       valid_paste_vs->attribVarCount, "attr_one") >= 0);

   /* Retail layout: uniform offsets in 4-byte words, block sizes in bytes. */
   GX2VertexShader *nsmbu_layout_vs = CompileVertexShader(
      "#version 150\n"
      NSMBU_MAT_BLOCK
      "layout(std140) uniform MdlEnvView {\n"
      "   vec4 cView[3];\n"
      "   vec4 cViewProj[4];\n"
      "   vec3 cLightDiffDir[8];\n"
      "   vec4 cLightDiffColor[8];\n"
      "   vec4 cAmbColor[2];\n"
      "   vec3 cFogColor[8];\n"
      "   float cFogStart[8];\n"
      "   float cFogStartEndInv[8];\n"
      "};\n"
      "layout(std140) uniform MdlMtx { vec4 cMtxPalette[192]; };\n"
      "layout(std140) uniform Shp { int cWeightNum; };\n"
      "in ivec4 aBlendIndex;\n"
      "in vec4 aBlendWeight;\n"
      "in vec3 aNormal;\n"
      "in vec3 aPosition;\n"
      "in vec2 aTexCoord0;\n"
      "void main() {\n"
      "   vec4 skinned = cMtxPalette[aBlendIndex.x] * aBlendWeight.x +\n"
      "                  cMtxPalette[aBlendIndex.y] * aBlendWeight.y;\n"
      "   vec4 env = cView[0] + cViewProj[3] +\n"
      "              vec4(cLightDiffDir[7], 0.0) + cLightDiffColor[6] +\n"
      "              cAmbColor[1] + vec4(cFogColor[5], 0.0) +\n"
      "              vec4(cFogStart[4] + cFogStartEndInv[3]);\n"
      "   gl_Position = skinned + env + " NSMBU_MAT_SUM " +\n"
      "                 vec4(aPosition, 1.0) + vec4(aNormal, 0.0) +\n"
      "                 vec4(aTexCoord0, 0.0, 0.0) + vec4(float(cWeightNum));\n"
      "}\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   if (!nsmbu_layout_vs) {
      fprintf(stderr, "NSMBU retail layout failed: %s\n", diagnostics);
      return 1;
   }

   /* Blocks run from bank 1 at the last declaration upwards. */
   CHECK(FindUniformBlock(nsmbu_layout_vs->uniformBlocks,
                          nsmbu_layout_vs->uniformBlockCount, "Shp") == 1);
   CHECK(FindUniformBlock(nsmbu_layout_vs->uniformBlocks,
                          nsmbu_layout_vs->uniformBlockCount, "MdlMtx") == 2);
   CHECK(FindUniformBlock(nsmbu_layout_vs->uniformBlocks,
                          nsmbu_layout_vs->uniformBlockCount, "MdlEnvView") == 3);
   CHECK(FindUniformBlock(nsmbu_layout_vs->uniformBlocks,
                          nsmbu_layout_vs->uniformBlockCount, "Mat") == 4);

   static const struct {
      const char *name;
      uint32_t size;
   } kNsmbuBlockSizes[] = {
      { "Shp", 4 }, { "MdlMtx", 3072 }, { "MdlEnvView", 784 }, { "Mat", 656 },
   };
   for (const auto &expected : kNsmbuBlockSizes) {
      const int index = FindUniformBlockIndex(nsmbu_layout_vs->uniformBlocks,
                                              nsmbu_layout_vs->uniformBlockCount,
                                              expected.name);
      CHECK(index >= 0);
      CHECK(nsmbu_layout_vs->uniformBlocks[index].size == expected.size);
   }

   static const struct {
      const char *name;
      uint32_t offset;
      uint32_t count;
      uint32_t type;
   } kNsmbuUniforms[] = {
      { "cView",            0,   3,   GX2_SHADER_VAR_TYPE_FLOAT4 },
      { "cViewProj",        12,  4,   GX2_SHADER_VAR_TYPE_FLOAT4 },
      { "cLightDiffDir",    28,  8,   GX2_SHADER_VAR_TYPE_FLOAT3 },
      { "cLightDiffColor",  60,  8,   GX2_SHADER_VAR_TYPE_FLOAT4 },
      { "cAmbColor",        92,  2,   GX2_SHADER_VAR_TYPE_FLOAT4 },
      { "cFogColor",        100, 8,   GX2_SHADER_VAR_TYPE_FLOAT3 },
      { "cFogStart",        132, 8,   GX2_SHADER_VAR_TYPE_FLOAT },
      { "cFogStartEndInv",  164, 8,   GX2_SHADER_VAR_TYPE_FLOAT },
      { "cMtxPalette",      0,   192, GX2_SHADER_VAR_TYPE_FLOAT4 },
      { "cWeightNum",       0,   1,   GX2_SHADER_VAR_TYPE_INT },
      { "mat_color0",       0,   1,   GX2_SHADER_VAR_TYPE_FLOAT4 },
      { "ind_texmtx0",      44,  2,   GX2_SHADER_VAR_TYPE_FLOAT4 },
      { "texmtx0",          68,  3,   GX2_SHADER_VAR_TYPE_FLOAT4 },
      { "texmtx7",          152, 3,   GX2_SHADER_VAR_TYPE_FLOAT4 },
   };
   for (const auto &expected : kNsmbuUniforms) {
      const GX2UniformVar *uniform = FindUniform(nsmbu_layout_vs->uniformVars,
                                                 nsmbu_layout_vs->uniformVarCount,
                                                 expected.name);
      CHECK(uniform);
      CHECK(uniform->offset == expected.offset);
      CHECK(uniform->count == expected.count);
      CHECK(uniform->type == expected.type);
   }

   /* Attribute locations follow declaration order. */
   static const char *const kNsmbuAttribs[] = {
      "aBlendIndex", "aBlendWeight", "aNormal", "aPosition", "aTexCoord0",
   };
   for (uint32_t i = 0; i < sizeof(kNsmbuAttribs) / sizeof(kNsmbuAttribs[0]); ++i) {
      CHECK(FindAttribute(nsmbu_layout_vs->attribVars,
                          nsmbu_layout_vs->attribVarCount,
                          kNsmbuAttribs[i]) == static_cast<int>(i));
   }

   /* The pixel stage's only block takes bank 1. */
   GX2PixelShader *nsmbu_layout_ps = CompilePixelShader(
      "#version 150\n"
      NSMBU_MAT_BLOCK
      "uniform sampler2D tex_map0;\n"
      "in vec4 vTexCoord;\n"
      "out vec4 oColor;\n"
      "void main() {\n"
      "   oColor = texture(tex_map0, vTexCoord.xy) * " NSMBU_MAT_SUM ";\n"
      "}\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   if (!nsmbu_layout_ps) {
      fprintf(stderr, "NSMBU retail pixel layout failed: %s\n", diagnostics);
      return 1;
   }
   CHECK(FindUniformBlock(nsmbu_layout_ps->uniformBlocks,
                          nsmbu_layout_ps->uniformBlockCount, "Mat") == 1);
   const int nsmbu_ps_mat = FindUniformBlockIndex(
      nsmbu_layout_ps->uniformBlocks, nsmbu_layout_ps->uniformBlockCount, "Mat");
   CHECK(nsmbu_ps_mat >= 0);
   CHECK(nsmbu_layout_ps->uniformBlocks[nsmbu_ps_mat].size == 656);
   CHECK(FindSampler(nsmbu_layout_ps->samplerVars,
                     nsmbu_layout_ps->samplerVarCount, "tex_map0") == 0);

   /* Uniform-block mode, empty ring, no stream out, on both stages. */
   CHECK(nsmbu_layout_vs->mode == GX2_SHADER_MODE_UNIFORM_BLOCK);
   CHECK(nsmbu_layout_ps->mode == GX2_SHADER_MODE_UNIFORM_BLOCK);
   CHECK(nsmbu_layout_vs->ringItemsize == 0);
   CHECK(!nsmbu_layout_vs->hasStreamOut);
   for (uint32_t i = 0; i < 4; ++i)
      CHECK(nsmbu_layout_vs->streamOutStride[i] == 0);

   /* Attributes record count 0. */
   static const struct {
      const char *name;
      uint32_t type;
   } kNsmbuAttribTypes[] = {
      { "aBlendIndex",  GX2_SHADER_VAR_TYPE_INT4 },
      { "aBlendWeight", GX2_SHADER_VAR_TYPE_FLOAT4 },
      { "aNormal",      GX2_SHADER_VAR_TYPE_FLOAT3 },
      { "aPosition",    GX2_SHADER_VAR_TYPE_FLOAT3 },
      { "aTexCoord0",   GX2_SHADER_VAR_TYPE_FLOAT2 },
   };
   for (const auto &expected : kNsmbuAttribTypes) {
      const GX2AttribVar *attrib = FindAttribVar(nsmbu_layout_vs->attribVars,
                                                 nsmbu_layout_vs->attribVarCount,
                                                 expected.name);
      CHECK(attrib);
      CHECK(attrib->type == expected.type);
      CHECK(attrib->count == 0);
   }

   /* A gap in the attribute names is not a gap in the locations. */
   GX2VertexShader *nsmbu_attrib_gap_vs = CompileVertexShader(
      "#version 150\n"
      "in ivec4 aBlendIndex;\n"
      "in vec4 aBlendWeight;\n"
      "in vec3 aNormal;\n"
      "in vec3 aPosition;\n"
      "in vec2 aTexCoord1;\n"
      "void main() {\n"
      "   gl_Position = vec4(aPosition, 1.0) + aBlendWeight + vec4(aNormal, 0.0) +\n"
      "                 vec4(aTexCoord1, 0.0, 0.0) + vec4(aBlendIndex);\n"
      "}\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   if (!nsmbu_attrib_gap_vs) {
      fprintf(stderr, "NSMBU sparse attribute set failed: %s\n", diagnostics);
      return 1;
   }
   static const char *const kNsmbuGapAttribs[] = {
      "aBlendIndex", "aBlendWeight", "aNormal", "aPosition", "aTexCoord1",
   };
   for (uint32_t i = 0; i < sizeof(kNsmbuGapAttribs) / sizeof(kNsmbuGapAttribs[0]); ++i) {
      CHECK(FindAttribute(nsmbu_attrib_gap_vs->attribVars,
                          nsmbu_attrib_gap_vs->attribVarCount,
                          kNsmbuGapAttribs[i]) == static_cast<int>(i));
   }

   /* The six-attribute retail set, with a colour. */
   GX2VertexShader *nsmbu_attrib_color_vs = CompileVertexShader(
      "#version 150\n"
      "in ivec4 aBlendIndex;\n"
      "in vec4 aBlendWeight;\n"
      "in vec4 aColor0;\n"
      "in vec3 aNormal;\n"
      "in vec3 aPosition;\n"
      "in vec2 aTexCoord0;\n"
      "void main() {\n"
      "   gl_Position = vec4(aPosition, 1.0) + aBlendWeight + aColor0 +\n"
      "                 vec4(aNormal, 0.0) + vec4(aTexCoord0, 0.0, 0.0) +\n"
      "                 vec4(aBlendIndex);\n"
      "}\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   if (!nsmbu_attrib_color_vs) {
      fprintf(stderr, "NSMBU coloured attribute set failed: %s\n", diagnostics);
      return 1;
   }
   static const char *const kNsmbuColorAttribs[] = {
      "aBlendIndex", "aBlendWeight", "aColor0", "aNormal", "aPosition", "aTexCoord0",
   };
   for (uint32_t i = 0; i < sizeof(kNsmbuColorAttribs) / sizeof(kNsmbuColorAttribs[0]); ++i) {
      CHECK(FindAttribute(nsmbu_attrib_color_vs->attribVars,
                          nsmbu_attrib_color_vs->attribVarCount,
                          kNsmbuColorAttribs[i]) == static_cast<int>(i));
   }

   /* Sampler units follow declaration order. */
   GX2PixelShader *nsmbu_sampler_gap_ps = CompilePixelShader(
      "#version 150\n"
      "uniform sampler2D tex_map0;\n"
      "uniform sampler2D tex_map2;\n"
      "in vec4 vTexCoord;\n"
      "out vec4 oColor;\n"
      "void main() {\n"
      "   oColor = texture(tex_map0, vTexCoord.xy) + texture(tex_map2, vTexCoord.zw);\n"
      "}\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   if (!nsmbu_sampler_gap_ps) {
      fprintf(stderr, "NSMBU sparse sampler set failed: %s\n", diagnostics);
      return 1;
   }
   CHECK(FindSampler(nsmbu_sampler_gap_ps->samplerVars,
                     nsmbu_sampler_gap_ps->samplerVarCount, "tex_map0") == 0);
   CHECK(FindSampler(nsmbu_sampler_gap_ps->samplerVars,
                     nsmbu_sampler_gap_ps->samplerVarCount, "tex_map2") == 1);

   /* Six consecutive units, all 2D. */
   GX2PixelShader *nsmbu_sampler_six_ps = CompilePixelShader(
      "#version 150\n"
      "uniform sampler2D tex_map0;\n"
      "uniform sampler2D tex_map1;\n"
      "uniform sampler2D tex_map2;\n"
      "uniform sampler2D tex_map3;\n"
      "uniform sampler2D tex_map4;\n"
      "uniform sampler2D tex_map5;\n"
      "in vec4 vTexCoord;\n"
      "out vec4 oColor;\n"
      "void main() {\n"
      "   oColor = texture(tex_map0, vTexCoord.xy) + texture(tex_map1, vTexCoord.xy) +\n"
      "            texture(tex_map2, vTexCoord.xy) + texture(tex_map3, vTexCoord.xy) +\n"
      "            texture(tex_map4, vTexCoord.xy) + texture(tex_map5, vTexCoord.xy);\n"
      "}\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   if (!nsmbu_sampler_six_ps) {
      fprintf(stderr, "NSMBU six-sampler set failed: %s\n", diagnostics);
      return 1;
   }
   static const char *const kNsmbuSamplers[] = {
      "tex_map0", "tex_map1", "tex_map2", "tex_map3", "tex_map4", "tex_map5",
   };
   for (uint32_t i = 0; i < sizeof(kNsmbuSamplers) / sizeof(kNsmbuSamplers[0]); ++i) {
      const GX2SamplerVar *sampler = FindSamplerVar(nsmbu_sampler_six_ps->samplerVars,
                                                    nsmbu_sampler_six_ps->samplerVarCount,
                                                    kNsmbuSamplers[i]);
      CHECK(sampler);
      CHECK(sampler->location == i);
      CHECK(sampler->type == GX2_SAMPLER_VAR_TYPE_SAMPLER_2D);
   }

   GX2PixelShader *invalid = CompilePixelShader(
      "#version 450\nthis is invalid;",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   CHECK(!invalid);
   CHECK(diagnostics[0]);

   FreeVertexShader(rio_primitive_vs);
   FreeVertexShader(rio_mvp_vs);
   FreeVertexShader(rio_named_varying_vs);
   FreeVertexShader(rio_implicit_block_vs);
   FreeVertexShader(rio_bound_block_vs);
   FreeVertexShader(pinned_block_vs);
   FreeVertexShader(implicit_version_vs);
   FreeVertexShader(implicit_version_pp_vs);
   FreeVertexShader(declared_version_vs);
   FreeVertexShader(implicit_version_compat_vs);
   FreeVertexShader(adjacent_paste_vs);
   FreeVertexShader(valid_paste_vs);
   FreeVertexShader(nsmbu_layout_vs);
   FreePixelShader(nsmbu_layout_ps);
   FreeVertexShader(nsmbu_attrib_gap_vs);
   FreeVertexShader(nsmbu_attrib_color_vs);
   FreePixelShader(nsmbu_sampler_gap_ps);
   FreePixelShader(nsmbu_sampler_six_ps);
   FreePixelShader(rio_primitive_ps);
   FreePixelShader(rio_mix_ps);
   FreePixelShader(rio_light_ps);
   FreePixelShader(rio_named_varying_ps);
   FreeVertexShader(vertex);
   FreeVertexShader(vertex_id);
   FreeVertexShader(loose_vertex);
   FreeVertexShader(last_uniform_block);
   FreeVertexShader(indexed_loose_with_block);
   FreeVertexShader(loose_pair_with_block);
   FreeVertexShader(mixed_uniforms);
   FreeVertexShader(oversized_mixed);
   FreePixelShader(pixel);
   FreePixelShader(environment_pass);
   DestroyGLSLCompiler();

   GX2VertexShader *still_initialized = CompileVertexShader(
      "#version 450\nvoid main() { gl_Position = vec4(0.0); }\n",
      diagnostics,
      sizeof(diagnostics),
      GLSL_COMPILER_FLAG_NONE);
   CHECK(still_initialized);
   FreeVertexShader(still_initialized);

   DestroyGLSLCompiler();
   CHECK(!CompileVertexShader(vertex_source,
                              diagnostics,
                              sizeof(diagnostics),
                              GLSL_COMPILER_FLAG_NONE));
   return 0;
}
