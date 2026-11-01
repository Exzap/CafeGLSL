#include "gfd.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <type_traits>
#include <vector>

constexpr uint32_t kFileMagic = 0x47667832;
constexpr uint32_t kBlockMagic = 0x424c4b7b;
constexpr uint32_t kRelocationMagic = 0x7d424c4b;
constexpr uint32_t kFileHeaderSize = 8 * 4;
constexpr uint32_t kBlockHeaderSize = 8 * 4;
constexpr uint32_t kRelocationHeaderSize = 10 * 4;
constexpr uint32_t kPatchData = 0xd0600000;
constexpr uint32_t kPatchText = 0xca700000;

enum class BlockType : uint32_t {
   EndOfFile = 1,
   Padding = 2,
   VertexShaderHeader = 3,
   VertexShaderProgram = 5,
   PixelShaderHeader = 6,
   PixelShaderProgram = 7,
   ComputeShaderHeader = 14,
   ComputeShaderProgram = 15,
};

struct DataPatch {
   size_t offset;
   size_t target = 0;
};

struct TextPatch {
   size_t offset;
   size_t target = 0;
   const char *text;
};

class Writer {
public:
   size_t size() const { return m_data.size(); }
   const std::vector<uint8_t> &data() const { return m_data; }

   void U32(uint32_t value)
   {
      const uint8_t big_endian[] = {
         static_cast<uint8_t>(value >> 24),
         static_cast<uint8_t>(value >> 16),
         static_cast<uint8_t>(value >> 8),
         static_cast<uint8_t>(value),
      };
      Bytes(big_endian, sizeof(big_endian));
   }

   void I32(int32_t value) { U32(static_cast<uint32_t>(value)); }

   void F32(float value)
   {
      uint32_t bits;
      memcpy(&bits, &value, sizeof(bits));
      U32(bits);
   }

   void U32At(size_t offset, uint32_t value)
   {
      m_data[offset] = static_cast<uint8_t>(value >> 24);
      m_data[offset + 1] = static_cast<uint8_t>(value >> 16);
      m_data[offset + 2] = static_cast<uint8_t>(value >> 8);
      m_data[offset + 3] = static_cast<uint8_t>(value);
   }

   void Bytes(const void *data, size_t size)
   {
      const size_t offset = m_data.size();
      m_data.resize(offset + size);
      if (size)
         memcpy(m_data.data() + offset, data, size);
   }

   void String(const char *text)
   {
      const size_t length = strlen(text) + 1;
      Bytes(text, length);
      m_data.resize((m_data.size() + 3) & ~size_t(3));
   }

private:
   std::vector<uint8_t> m_data;
};

static void WriteRBuffer(Writer &writer, const GX2RBuffer &buffer)
{
   writer.U32(buffer.flags);
   writer.U32(buffer.elemSize);
   writer.U32(buffer.elemCount);
   writer.U32(0);
}

static void WriteUniformBlocks(Writer &writer,
                               DataPatch patch,
                               std::vector<DataPatch> &data_patches,
                               std::vector<TextPatch> &text_patches,
                               const GX2UniformBlock *blocks,
                               uint32_t count)
{
   if (!count)
      return;
   patch.target = writer.size();
   data_patches.push_back(patch);
   for (uint32_t i = 0; i < count; ++i) {
      text_patches.push_back({writer.size(), 0, blocks[i].name});
      writer.U32(0);
      writer.U32(blocks[i].offset);
      writer.U32(blocks[i].size);
   }
}

static void WriteUniforms(Writer &writer,
                          DataPatch patch,
                          std::vector<DataPatch> &data_patches,
                          std::vector<TextPatch> &text_patches,
                          const GX2UniformVar *uniforms,
                          uint32_t count)
{
   if (!count)
      return;
   patch.target = writer.size();
   data_patches.push_back(patch);
   for (uint32_t i = 0; i < count; ++i) {
      text_patches.push_back({writer.size(), 0, uniforms[i].name});
      writer.U32(0);
      writer.U32(uniforms[i].type);
      writer.U32(uniforms[i].count);
      writer.U32(uniforms[i].offset);
      writer.I32(uniforms[i].block);
   }
}

static void WriteInitialValues(Writer &writer,
                               DataPatch patch,
                               std::vector<DataPatch> &data_patches,
                               const GX2UniformInitialValue *values,
                               uint32_t count)
{
   if (!count)
      return;
   patch.target = writer.size();
   data_patches.push_back(patch);
   for (uint32_t i = 0; i < count; ++i) {
      for (float value : values[i].value)
         writer.F32(value);
      writer.U32(values[i].offset);
   }
}

static void WriteLoops(Writer &writer,
                       DataPatch patch,
                       std::vector<DataPatch> &data_patches,
                       const GX2LoopVar *loops,
                       uint32_t count)
{
   if (!count)
      return;
   patch.target = writer.size();
   data_patches.push_back(patch);
   for (uint32_t i = 0; i < count; ++i) {
      writer.U32(loops[i].offset);
      writer.U32(loops[i].value);
   }
}

static void WriteSamplers(Writer &writer,
                          DataPatch patch,
                          std::vector<DataPatch> &data_patches,
                          std::vector<TextPatch> &text_patches,
                          const GX2SamplerVar *samplers,
                          uint32_t count)
{
   if (!count)
      return;
   patch.target = writer.size();
   data_patches.push_back(patch);
   for (uint32_t i = 0; i < count; ++i) {
      text_patches.push_back({writer.size(), 0, samplers[i].name});
      writer.U32(0);
      writer.U32(samplers[i].type);
      writer.U32(samplers[i].location);
   }
}

static void WriteAttributes(Writer &writer,
                            DataPatch patch,
                            std::vector<DataPatch> &data_patches,
                            std::vector<TextPatch> &text_patches,
                            const GX2AttribVar *attributes,
                            uint32_t count)
{
   if (!count)
      return;
   patch.target = writer.size();
   data_patches.push_back(patch);
   for (uint32_t i = 0; i < count; ++i) {
      text_patches.push_back({writer.size(), 0, attributes[i].name});
      writer.U32(0);
      writer.U32(attributes[i].type);
      writer.U32(attributes[i].count);
      writer.U32(attributes[i].location);
   }
}

static void FinishRelocations(Writer &writer,
                              std::vector<DataPatch> &data_patches,
                              std::vector<TextPatch> &text_patches)
{
   const size_t text_offset = writer.size();
   for (TextPatch &patch : text_patches) {
      patch.target = writer.size();
      writer.String(patch.text);
   }
   const size_t text_size = writer.size() - text_offset;
   const size_t patch_offset = writer.size();

   for (const DataPatch &patch : data_patches) {
      writer.U32At(patch.offset, static_cast<uint32_t>(patch.target) | kPatchData);
      writer.U32(static_cast<uint32_t>(patch.offset) | kPatchData);
   }
   for (const TextPatch &patch : text_patches) {
      writer.U32At(patch.offset, static_cast<uint32_t>(patch.target) | kPatchText);
      writer.U32(static_cast<uint32_t>(patch.offset) | kPatchText);
   }

   writer.U32(kRelocationMagic);
   writer.U32(kRelocationHeaderSize);
   writer.U32(0);
   writer.U32(static_cast<uint32_t>(patch_offset));
   writer.U32(kPatchData);
   writer.U32(static_cast<uint32_t>(text_size));
   writer.U32(static_cast<uint32_t>(text_offset) | kPatchData);
   writer.U32(0);
   writer.U32(data_patches.size() + text_patches.size());
   writer.U32(static_cast<uint32_t>(patch_offset) | kPatchData);
}

static Writer WriteVertexHeader(const GX2VertexShader &shader)
{
   Writer writer;
   std::vector<DataPatch> data_patches;
   std::vector<TextPatch> text_patches;

   writer.U32(shader.regs.sq_pgm_resources_vs);
   writer.U32(shader.regs.vgt_primitiveid_en);
   writer.U32(shader.regs.spi_vs_out_config);
   writer.U32(shader.regs.num_spi_vs_out_id);
   for (uint32_t value : shader.regs.spi_vs_out_id)
      writer.U32(value);
   writer.U32(shader.regs.pa_cl_vs_out_cntl);
   writer.U32(shader.regs.sq_vtx_semantic_clear);
   writer.U32(shader.regs.num_sq_vtx_semantic);
   for (uint32_t value : shader.regs.sq_vtx_semantic)
      writer.U32(value);
   writer.U32(shader.regs.vgt_strmout_buffer_en);
   writer.U32(shader.regs.vgt_vertex_reuse_block_cntl);
   writer.U32(shader.regs.vgt_hos_reuse_depth);
   writer.U32(shader.size);
   writer.U32(0);
   writer.U32(shader.mode);

   DataPatch blocks{writer.size() + 4};
   writer.U32(shader.uniformBlockCount);
   writer.U32(0);
   DataPatch uniforms{writer.size() + 4};
   writer.U32(shader.uniformVarCount);
   writer.U32(0);
   DataPatch initial_values{writer.size() + 4};
   writer.U32(shader.initialValueCount);
   writer.U32(0);
   DataPatch loops{writer.size() + 4};
   writer.U32(shader.loopVarCount);
   writer.U32(0);
   DataPatch samplers{writer.size() + 4};
   writer.U32(shader.samplerVarCount);
   writer.U32(0);
   DataPatch attributes{writer.size() + 4};
   writer.U32(shader.attribVarCount);
   writer.U32(0);

   writer.U32(shader.ringItemsize);
   writer.U32(shader.hasStreamOut ? 1 : 0);
   for (uint32_t stride : shader.streamOutStride)
      writer.U32(stride);
   WriteRBuffer(writer, shader.gx2rBuffer);

   WriteUniformBlocks(writer, blocks, data_patches, text_patches,
                      shader.uniformBlocks, shader.uniformBlockCount);
   WriteUniforms(writer, uniforms, data_patches, text_patches,
                 shader.uniformVars, shader.uniformVarCount);
   WriteInitialValues(writer, initial_values, data_patches,
                      shader.initialValues, shader.initialValueCount);
   WriteLoops(writer, loops, data_patches, shader.loopVars, shader.loopVarCount);
   WriteSamplers(writer, samplers, data_patches, text_patches,
                 shader.samplerVars, shader.samplerVarCount);
   WriteAttributes(writer, attributes, data_patches, text_patches,
                   shader.attribVars, shader.attribVarCount);
   FinishRelocations(writer, data_patches, text_patches);
   return writer;
}

static Writer WritePixelHeader(const GX2PixelShader &shader)
{
   Writer writer;
   std::vector<DataPatch> data_patches;
   std::vector<TextPatch> text_patches;

   writer.U32(shader.regs.sq_pgm_resources_ps);
   writer.U32(shader.regs.sq_pgm_exports_ps);
   writer.U32(shader.regs.spi_ps_in_control_0);
   writer.U32(shader.regs.spi_ps_in_control_1);
   writer.U32(shader.regs.num_spi_ps_input_cntl);
   for (uint32_t value : shader.regs.spi_ps_input_cntls)
      writer.U32(value);
   writer.U32(shader.regs.cb_shader_mask);
   writer.U32(shader.regs.cb_shader_control);
   writer.U32(shader.regs.db_shader_control);
   writer.U32(shader.regs.spi_input_z);
   writer.U32(shader.size);
   writer.U32(0);
   writer.U32(shader.mode);

   DataPatch blocks{writer.size() + 4};
   writer.U32(shader.uniformBlockCount);
   writer.U32(0);
   DataPatch uniforms{writer.size() + 4};
   writer.U32(shader.uniformVarCount);
   writer.U32(0);
   DataPatch initial_values{writer.size() + 4};
   writer.U32(shader.initialValueCount);
   writer.U32(0);
   DataPatch loops{writer.size() + 4};
   writer.U32(shader.loopVarCount);
   writer.U32(0);
   DataPatch samplers{writer.size() + 4};
   writer.U32(shader.samplerVarCount);
   writer.U32(0);
   WriteRBuffer(writer, shader.gx2rBuffer);

   WriteUniformBlocks(writer, blocks, data_patches, text_patches,
                      shader.uniformBlocks, shader.uniformBlockCount);
   WriteUniforms(writer, uniforms, data_patches, text_patches,
                 shader.uniformVars, shader.uniformVarCount);
   WriteInitialValues(writer, initial_values, data_patches,
                      shader.initialValues, shader.initialValueCount);
   WriteLoops(writer, loops, data_patches, shader.loopVars, shader.loopVarCount);
   WriteSamplers(writer, samplers, data_patches, text_patches,
                 shader.samplerVars, shader.samplerVarCount);
   FinishRelocations(writer, data_patches, text_patches);
   return writer;
}

static Writer WriteComputeHeader(const GX2ComputeShader &shader)
{
   Writer writer;
   std::vector<DataPatch> data_patches;
   std::vector<TextPatch> text_patches;

   for (uint32_t value : shader.regs)
      writer.U32(value);
   writer.U32(shader.size);
   writer.U32(0);

   DataPatch blocks{writer.size() + 4};
   writer.U32(shader.uniformBlockCount);
   writer.U32(0);
   DataPatch uniforms{writer.size() + 4};
   writer.U32(shader.uniformVarCount);
   writer.U32(0);
   DataPatch initial_values{writer.size() + 4};
   writer.U32(shader.initialValueCount);
   writer.U32(0);
   DataPatch loops{writer.size() + 4};
   writer.U32(shader.loopVarCount);
   writer.U32(0);
   DataPatch samplers{writer.size() + 4};
   writer.U32(shader.samplerVarCount);
   writer.U32(0);

   writer.U32(shader.workgroupSizeX);
   writer.U32(shader.workgroupSizeY);
   writer.U32(shader.workgroupSizeZ);
   writer.U32(shader.over64Mode);
   writer.U32(shader.numWavesPerSimd);
   WriteRBuffer(writer, shader.gx2rBuffer);

   WriteUniformBlocks(writer, blocks, data_patches, text_patches,
                      shader.uniformBlocks, shader.uniformBlockCount);
   WriteUniforms(writer, uniforms, data_patches, text_patches,
                 shader.uniformVars, shader.uniformVarCount);
   WriteInitialValues(writer, initial_values, data_patches,
                      shader.initialValues, shader.initialValueCount);
   WriteLoops(writer, loops, data_patches, shader.loopVars, shader.loopVarCount);
   WriteSamplers(writer, samplers, data_patches, text_patches,
                 shader.samplerVars, shader.samplerVarCount);
   FinishRelocations(writer, data_patches, text_patches);
   return writer;
}

static void WriteBlock(Writer &output,
                       BlockType type,
                       uint32_t id,
                       uint32_t index,
                       const void *data,
                       size_t size)
{
   output.U32(kBlockMagic);
   output.U32(kBlockHeaderSize);
   output.U32(1);
   output.U32(0);
   output.U32(static_cast<uint32_t>(type));
   output.U32(size);
   output.U32(id);
   output.U32(index);
   output.Bytes(data, size);
}

static void AlignNextBlock(Writer &output, uint32_t &block_id)
{
   size_t padding = (0x200 - ((output.size() + kBlockHeaderSize) & 0x1ff)) & 0x1ff;
   if (!padding)
      return;
   if (padding < kBlockHeaderSize)
      padding += 0x200;
   padding -= kBlockHeaderSize;
   std::vector<uint8_t> zeroes(padding);
   WriteBlock(output, BlockType::Padding, block_id++, 0,
              zeroes.data(), zeroes.size());
}

bool WriteGFD(const GFDFile &file, const std::string &path, bool align_blocks)
{
   Writer output;
   uint32_t block_id = 0;
   output.U32(kFileMagic);
   output.U32(kFileHeaderSize);
   output.U32(7);
   output.U32(1);
   output.U32(2);
   output.U32(align_blocks ? 1 : 0);
   output.U32(0);
   output.U32(0);

   for (uint32_t i = 0; i < file.vertexShaders.size(); ++i) {
      const GX2VertexShader &shader = *file.vertexShaders[i];
      Writer header = WriteVertexHeader(shader);
      WriteBlock(output, BlockType::VertexShaderHeader, block_id++, i,
                 header.data().data(), header.size());
      if (align_blocks)
         AlignNextBlock(output, block_id);
      WriteBlock(output, BlockType::VertexShaderProgram, block_id++, i,
                 shader.program, shader.size);
   }

   for (uint32_t i = 0; i < file.pixelShaders.size(); ++i) {
      const GX2PixelShader &shader = *file.pixelShaders[i];
      Writer header = WritePixelHeader(shader);
      WriteBlock(output, BlockType::PixelShaderHeader, block_id++, i,
                 header.data().data(), header.size());
      if (align_blocks)
         AlignNextBlock(output, block_id);
      WriteBlock(output, BlockType::PixelShaderProgram, block_id++, i,
                 shader.program, shader.size);
   }

   for (uint32_t i = 0; i < file.computeShaders.size(); ++i) {
      const GX2ComputeShader &shader = *file.computeShaders[i];
      Writer header = WriteComputeHeader(shader);
      WriteBlock(output, BlockType::ComputeShaderHeader, block_id++, i,
                 header.data().data(), header.size());
      if (align_blocks)
         AlignNextBlock(output, block_id);
      WriteBlock(output, BlockType::ComputeShaderProgram, block_id++, i,
                 shader.program, shader.size);
   }

   WriteBlock(output, BlockType::EndOfFile, block_id, 0, nullptr, 0);
   std::ofstream file_stream(path, std::ios::binary);
   if (!file_stream)
      return false;
   file_stream.write(reinterpret_cast<const char *>(output.data().data()),
                     output.data().size());
   return file_stream.good();
}
