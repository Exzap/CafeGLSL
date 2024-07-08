#include "gfd.h"
#include <fstream>
#include <cstring>
#include <string.h>
#include <bit>
#include <vector>

template<typename Type>
constexpr inline Type align_up(Type value, size_t alignment)
{
   return static_cast<Type>((static_cast<size_t>(value) + (alignment - 1)) & ~(alignment - 1));
}

template<typename Type>
constexpr inline Type align_down(Type value, size_t alignment)
{
   return static_cast<Type>(static_cast<size_t>(value) & ~(alignment - 1));
}

template<typename Type>
constexpr inline Type * align_up(Type *value, size_t alignment)
{
   return reinterpret_cast<Type*>((reinterpret_cast<size_t>(value) + (alignment - 1)) & ~(alignment - 1));
}

template<typename Type>
constexpr inline Type * align_down(Type *value, size_t alignment)
{
   return reinterpret_cast<Type*>(reinterpret_cast<size_t>(value) & ~(alignment - 1));
}

template<typename Type>
constexpr bool align_check(Type *value, size_t alignment)
{
   return (reinterpret_cast<size_t>(value) & (alignment - 1)) == 0;
}

template<typename Type>
constexpr bool align_check(Type value, size_t alignment)
{
   return (static_cast<size_t>(value) & (alignment - 1)) == 0;
}

template<typename Type>
const auto byte_swap(Type value)
{
    // convert float
    if constexpr (std::is_same_v<Type, float>)
    {
        uint32_t temp;
        std::memcpy(&temp, &value, sizeof(float));
        temp = byte_swap(temp);
        std::memcpy(&value, &temp, sizeof(float));
        return value;
    }
    // convert double
    else if constexpr (std::is_same_v<Type, double>)
    {
        uint64_t temp;
        std::memcpy(&temp, &value, sizeof(double));
        temp = byte_swap(temp);
        std::memcpy(&value, &temp, sizeof(double));
        return value;
    }
    // convert ints
    if constexpr (std::is_integral_v<Type>)
    {
        if constexpr (sizeof(Type) == 1)
        {
            return value;
        }
        else if constexpr (sizeof(Type) == 2)
        {
            return static_cast<Type>((value >> 8) | (value << 8));
        }
        else if constexpr (sizeof(Type) == 4)
        {
            return static_cast<Type>((value >> 24) | ((value << 8) & 0x00FF0000) | ((value >> 8) & 0x0000FF00) | (value << 24));
        }
        else if constexpr (sizeof(Type) == 8)
        {
            return static_cast<Type>((value >> 56) | ((value << 40) & 0x00FF000000000000) | ((value << 24) & 0x0000FF0000000000) | ((value << 8) & 0x000000FF00000000) | ((value >> 8) & 0x00000000FF000000) | ((value >> 24) & 0x0000000000FF0000) | ((value >> 40) & 0x000000000000FF00) | (value << 56));
        }
    }
}


struct DataPatch
{
    size_t offset;
    size_t target;
};

struct TextPatch
{
    size_t offset;
    size_t target;
    const char *text;
};

using MemoryFile = std::vector<uint8_t>;

template <typename Type>
inline void writeAt(MemoryFile &fh, size_t pos, Type value)
{
    *reinterpret_cast<Type *>(fh.data() + pos) = byte_swap(value);
}

template <typename Type>
inline void write(MemoryFile &fh, Type value)
{
    auto pos = fh.size();
    fh.resize(pos + sizeof(Type));
    *reinterpret_cast<Type *>(fh.data() + pos) = byte_swap(value);
}

inline void writeNullTerminatedString(MemoryFile &fh, const char *str)
{
    auto pos = fh.size();
    auto len = strlen(str) + 1;
    fh.resize(align_up(pos + len, 4));
    std::memcpy(fh.data() + pos, str, len);
}

inline void writeBinary(MemoryFile &fh, const std::vector<uint8_t> &data)
{
    auto pos = fh.size();
    auto len = data.size();
    fh.resize(pos + len);
    std::memcpy(fh.data() + pos, data.data(), len);
}

static bool writeGX2RBuffer(MemoryFile &fh, const GX2RBuffer &buffer)
{
    write<uint32_t>(fh, static_cast<uint32_t>(buffer.flags));
    write<uint32_t>(fh, buffer.elemSize);
    write<uint32_t>(fh, buffer.elemCount);
    write<uint32_t>(fh, 0); // buffer.buffer
    return true;
}

static bool writeUniformBlocksData(std::vector<uint8_t> &fh, DataPatch &patch, std::vector<DataPatch> &dataPatches, std::vector<TextPatch> &textPatches, const std::vector<GX2UniformBlock> &uniformBlocks)
{
    if (uniformBlocks.size() > 0)
    {
        patch.target = fh.size();
        dataPatches.push_back(patch);

        for (auto &block : uniformBlocks)
        {
            textPatches.push_back(TextPatch{fh.size(), 0, block.name});
            write<uint32_t>(fh, 0); // block.name
            write<uint32_t>(fh, block.offset);
            write<uint32_t>(fh, block.size);
        }
    }

    return true;
}

static bool writeUniformVarsData(std::vector<uint8_t> &fh, DataPatch &patch, std::vector<DataPatch> &dataPatches, std::vector<TextPatch> &textPatches, const std::vector<GX2UniformVar> &uniformVars)
{
    if (uniformVars.size() > 0)
    {
        patch.target = fh.size();
        dataPatches.push_back(patch);

        for (auto &var : uniformVars)
        {
            textPatches.push_back(TextPatch{fh.size(), 0, var.name});
            write<uint32_t>(fh, 0); // var.name
            write<uint32_t>(fh, static_cast<uint32_t>(var.type));
            write<uint32_t>(fh, var.count);
            write<uint32_t>(fh, var.offset);
            write<int32_t>(fh, var.block);
        }
    }

    return true;
}

static bool writeInitialValuesData(std::vector<uint8_t> &fh, DataPatch &patch, std::vector<DataPatch> &dataPatches, std::vector<TextPatch> &textPatches, const std::vector<GX2UniformInitialValue> &initialValues)
{
    if (initialValues.size() > 0)
    {
        patch.target = fh.size();
        dataPatches.push_back(patch);

        for (auto &var : initialValues)
        {
            for (auto &value : var.value)
            {
                write<float>(fh, value);
            }

            write<uint32_t>(fh, var.offset);
        }
    }

    return true;
}

static bool writeLoopVarsData(std::vector<uint8_t> &fh, DataPatch &patch, std::vector<DataPatch> &dataPatches, std::vector<TextPatch> &textPatches, const std::vector<GX2LoopVar> &loopVars)
{
    if (loopVars.size() > 0)
    {
        patch.target = fh.size();
        dataPatches.push_back(patch);

        for (auto &var : loopVars)
        {
            write<uint32_t>(fh, var.offset);
            write<uint32_t>(fh, var.value);
        }
    }

    return true;
}

static bool writeSamplerVarsData(std::vector<uint8_t> &fh, DataPatch &patch, std::vector<DataPatch> &dataPatches, std::vector<TextPatch> &textPatches, const std::vector<GX2SamplerVar> &samplerVars)
{
    if (samplerVars.size() > 0)
    {
        patch.target = fh.size();
        dataPatches.push_back(patch);

        for (auto &var : samplerVars)
        {
            textPatches.push_back(TextPatch{fh.size(), 0, var.name});
            write<uint32_t>(fh, 0); // var.name
            write<uint32_t>(fh, static_cast<uint32_t>(var.type));
            write<uint32_t>(fh, var.location);
        }
    }

    return true;
}

static bool writeAttribVarsData(std::vector<uint8_t> &fh, DataPatch &patch, std::vector<DataPatch> &dataPatches, std::vector<TextPatch> &textPatches, const std::vector<GX2AttribVar> &attribVars)
{
    if (attribVars.size() > 0)
    {
        patch.target = fh.size();
        dataPatches.push_back(patch);

        for (auto &var : attribVars)
        {
            textPatches.push_back(TextPatch{fh.size(), 0, var.name});
            write<uint32_t>(fh, 0); // var.name
            write<uint32_t>(fh, static_cast<uint32_t>(var.type));
            write<uint32_t>(fh, var.count);
            write<uint32_t>(fh, var.location);
        }
    }

    return true;
}

static bool writeRelocationData(std::vector<uint8_t> &fh, std::vector<DataPatch> &dataPatches, std::vector<TextPatch> &textPatches)
{
    // Now write text
    auto textOffset = fh.size();

    for (auto &patch : textPatches)
    {
        patch.target = fh.size();
        writeNullTerminatedString(fh, patch.text);
    }

    auto textSize = fh.size() - textOffset;

    auto patchOffset = fh.size();
    auto dataSize = patchOffset;
    auto dataOffset = 0;

    // Now write patches
    for (auto &patch : dataPatches)
    {
        writeAt<uint32_t>(fh, patch.offset, static_cast<uint32_t>(patch.target) | GFDPatchData);
        write<uint32_t>(fh, static_cast<uint32_t>(patch.offset) | GFDPatchData);
    }

    for (auto &patch : textPatches)
    {
        writeAt<uint32_t>(fh, patch.offset, static_cast<uint32_t>(patch.target) | GFDPatchText);
        write<uint32_t>(fh, static_cast<uint32_t>(patch.offset) | GFDPatchText);
    }

    // Write relocation header
    write<uint32_t>(fh, GFDRelocationHeader::Magic);
    write<uint32_t>(fh, GFDRelocationHeader::HeaderSize);
    write<uint32_t>(fh, 0); // unk1
    write<uint32_t>(fh, static_cast<uint32_t>(dataSize));
    write<uint32_t>(fh, static_cast<uint32_t>(dataOffset) | GFDPatchData);
    write<uint32_t>(fh, static_cast<uint32_t>(textSize));
    write<uint32_t>(fh, static_cast<uint32_t>(textOffset) | GFDPatchData);
    write<uint32_t>(fh, 0); // patchBase
    write<uint32_t>(fh, static_cast<uint32_t>(dataPatches.size() + textPatches.size()));
    write<uint32_t>(fh, static_cast<uint32_t>(patchOffset) | GFDPatchData);
    return true;
}

static bool writeVertexShader(std::vector<uint8_t> &fh, const GX2VertexShader &vsh)
{
    std::vector<uint8_t> text;
    std::vector<DataPatch> dataPatches;
    std::vector<TextPatch> textPatches;

    write<uint32_t>(fh, vsh.regs.sq_pgm_resources_vs);
    write<uint32_t>(fh, vsh.regs.vgt_primitiveid_en);
    write<uint32_t>(fh, vsh.regs.spi_vs_out_config);
    write<uint32_t>(fh, vsh.regs.num_spi_vs_out_id);

    for (auto &spi_vs_out_id : vsh.regs.spi_vs_out_id)
    {
        write<uint32_t>(fh, spi_vs_out_id);
    }

    write<uint32_t>(fh, vsh.regs.pa_cl_vs_out_cntl);
    write<uint32_t>(fh, vsh.regs.sq_vtx_semantic_clear);
    write<uint32_t>(fh, vsh.regs.num_sq_vtx_semantic);

    for (auto &sq_vtx_semantic : vsh.regs.sq_vtx_semantic)
    {
        write<uint32_t>(fh, sq_vtx_semantic);
    }

    write<uint32_t>(fh, vsh.regs.vgt_strmout_buffer_en);
    write<uint32_t>(fh, vsh.regs.vgt_vertex_reuse_block_cntl);
    write<uint32_t>(fh, vsh.regs.vgt_hos_reuse_depth);

    write<uint32_t>(fh, static_cast<uint32_t>(vsh.size));
    write<uint32_t>(fh, 0); // vsh.data
    write<uint32_t>(fh, static_cast<uint32_t>(vsh.mode));

    auto uniformBlocksPatch = DataPatch{fh.size() + 4, 0};
    write<uint32_t>(fh, static_cast<uint32_t>(vsh.uniformBlockCount));
    write<uint32_t>(fh, 0); // vsh.uniformBlocks.data

    auto uniformVarsPatch = DataPatch{fh.size() + 4, 0};
    write<uint32_t>(fh, static_cast<uint32_t>(vsh.uniformVarCount));
    write<uint32_t>(fh, 0); // vsh.uniformVars.data

    auto initialValuesPatch = DataPatch{fh.size() + 4, 0};
    write<uint32_t>(fh, static_cast<uint32_t>(vsh.initialValueCount));
    write<uint32_t>(fh, 0); // vsh.initialValues.data

    auto loopVarsPatch = DataPatch{fh.size() + 4, 0};
    write<uint32_t>(fh, static_cast<uint32_t>(vsh.loopVarCount));
    write<uint32_t>(fh, 0); // vsh.loopVars.data

    auto samplerVarsPatch = DataPatch{fh.size() + 4, 0};
    write<uint32_t>(fh, static_cast<uint32_t>(vsh.samplerVarCount));
    write<uint32_t>(fh, 0); // vsh.samplerVars.data

    auto attribVarsPatch = DataPatch{fh.size() + 4, 0};
    write<uint32_t>(fh, static_cast<uint32_t>(vsh.attribVarCount));
    write<uint32_t>(fh, 0); // vsh.attribVars.data

    write<uint32_t>(fh, vsh.ringItemsize);
    write<uint32_t>(fh, vsh.hasStreamOut ? 1 : 0);

    for (auto &stride : vsh.streamOutStride)
    {
        write<uint32_t>(fh, stride);
    }

    writeGX2RBuffer(fh, vsh.gx2rBuffer);

    // Now write relocated data
    writeUniformBlocksData(fh, uniformBlocksPatch, dataPatches, textPatches, std::vector<GX2UniformBlock>(vsh.uniformBlocks, vsh.uniformBlocks + vsh.uniformBlockCount));
    writeUniformVarsData(fh, uniformVarsPatch, dataPatches, textPatches, std::vector<GX2UniformVar>(vsh.uniformVars, vsh.uniformVars + vsh.uniformVarCount));
    writeInitialValuesData(fh, initialValuesPatch, dataPatches, textPatches, std::vector<GX2UniformInitialValue>(vsh.initialValues, vsh.initialValues + vsh.initialValueCount));
    writeLoopVarsData(fh, loopVarsPatch, dataPatches, textPatches, std::vector<GX2LoopVar>(vsh.loopVars, vsh.loopVars + vsh.loopVarCount));
    writeSamplerVarsData(fh, samplerVarsPatch, dataPatches, textPatches, std::vector<GX2SamplerVar>(vsh.samplerVars, vsh.samplerVars + vsh.samplerVarCount));
    writeAttribVarsData(fh, attribVarsPatch, dataPatches, textPatches, std::vector<GX2AttribVar>(vsh.attribVars, vsh.attribVars + vsh.attribVarCount));
    writeRelocationData(fh, dataPatches, textPatches);
    return true;
}

static bool writePixelShader(std::vector<uint8_t> &fh, const GX2PixelShader &psh)
{
    std::vector<uint8_t> text;
    std::vector<DataPatch> dataPatches;
    std::vector<TextPatch> textPatches;

    write<uint32_t>(fh, psh.regs.sq_pgm_resources_ps);
    write<uint32_t>(fh, psh.regs.sq_pgm_exports_ps);
    write<uint32_t>(fh, psh.regs.spi_ps_in_control_0);
    write<uint32_t>(fh, psh.regs.spi_ps_in_control_1);
    write<uint32_t>(fh, psh.regs.num_spi_ps_input_cntl);

    for (auto &spi_ps_input_cntl : psh.regs.spi_ps_input_cntls)
    {
        write<uint32_t>(fh, spi_ps_input_cntl);
    }

    write<uint32_t>(fh, psh.regs.cb_shader_mask);
    write<uint32_t>(fh, psh.regs.cb_shader_control);
    write<uint32_t>(fh, psh.regs.db_shader_control);
    write<uint32_t>(fh, psh.regs.spi_input_z);

    write<uint32_t>(fh, static_cast<uint32_t>(psh.size));
    write<uint32_t>(fh, 0); // psh.data
    write<uint32_t>(fh, static_cast<uint32_t>(psh.mode));

    auto uniformBlocksPatch = DataPatch{fh.size() + 4, 0};
    write<uint32_t>(fh, static_cast<uint32_t>(psh.uniformBlockCount));
    write<uint32_t>(fh, 0); // vsh.uniformBlocks.data

    auto uniformVarsPatch = DataPatch{fh.size() + 4, 0};
    write<uint32_t>(fh, static_cast<uint32_t>(psh.uniformVarCount));
    write<uint32_t>(fh, 0); // vsh.uniformVars.data

    auto initialValuesPatch = DataPatch{fh.size() + 4, 0};
    write<uint32_t>(fh, static_cast<uint32_t>(psh.initialValueCount));
    write<uint32_t>(fh, 0); // vsh.initialValues.data

    auto loopVarsPatch = DataPatch{fh.size() + 4, 0};
    write<uint32_t>(fh, static_cast<uint32_t>(psh.loopVarCount));
    write<uint32_t>(fh, 0); // vsh.loopVars.data

    auto samplerVarsPatch = DataPatch{fh.size() + 4, 0};
    write<uint32_t>(fh, static_cast<uint32_t>(psh.samplerVarCount));
    write<uint32_t>(fh, 0); // vsh.samplerVars.data

    writeGX2RBuffer(fh, psh.gx2rBuffer);

    // Now write relocated data
    writeUniformBlocksData(fh, uniformBlocksPatch, dataPatches, textPatches, std::vector<GX2UniformBlock>(psh.uniformBlocks, psh.uniformBlocks + psh.uniformBlockCount));
    writeUniformVarsData(fh, uniformVarsPatch, dataPatches, textPatches, std::vector<GX2UniformVar>(psh.uniformVars, psh.uniformVars + psh.uniformVarCount));
    writeInitialValuesData(fh, initialValuesPatch, dataPatches, textPatches, std::vector<GX2UniformInitialValue>(psh.initialValues, psh.initialValues + psh.initialValueCount));
    writeLoopVarsData(fh, loopVarsPatch, dataPatches, textPatches, std::vector<GX2LoopVar>(psh.loopVars, psh.loopVars + psh.loopVarCount));
    writeSamplerVarsData(fh, samplerVarsPatch, dataPatches, textPatches, std::vector<GX2SamplerVar>(psh.samplerVars, psh.samplerVars + psh.samplerVarCount));
    writeRelocationData(fh, dataPatches, textPatches);
    return true;
}

static bool writeGeometryShader(std::vector<uint8_t> &fh, const GX2GeometryShader &gsh)
{
    std::vector<uint8_t> text;
    std::vector<DataPatch> dataPatches;
    std::vector<TextPatch> textPatches;

    write<uint32_t>(fh, gsh.regs.sq_pgm_resources_gs);
    write<uint32_t>(fh, gsh.regs.vgt_gs_out_prim_type);
    write<uint32_t>(fh, gsh.regs.vgt_gs_mode);
    write<uint32_t>(fh, gsh.regs.pa_cl_vs_out_cntl);
    write<uint32_t>(fh, gsh.regs.sq_pgm_resources_vs);
    write<uint32_t>(fh, gsh.regs.sq_gs_vert_itemsize);
    write<uint32_t>(fh, gsh.regs.spi_vs_out_config);
    write<uint32_t>(fh, gsh.regs.num_spi_vs_out_id);

    for (auto &spi_vs_out_id : gsh.regs.spi_vs_out_id)
    {
        write<uint32_t>(fh, spi_vs_out_id);
    }

    write<uint32_t>(fh, gsh.regs.vgt_strmout_buffer_en);

    write<uint32_t>(fh, static_cast<uint32_t>(gsh.size));
    write<uint32_t>(fh, 0); // gsh.data

    write<uint32_t>(fh, static_cast<uint32_t>(gsh.vertexProgramSize));
    write<uint32_t>(fh, 0); // gsh.vertexShaderData

    write<uint32_t>(fh, static_cast<uint32_t>(gsh.mode));

    auto uniformBlocksPatch = DataPatch{fh.size() + 4, 0};
    write<uint32_t>(fh, static_cast<uint32_t>(gsh.uniformBlockCount));
    write<uint32_t>(fh, 0); // vsh.uniformBlocks.data

    auto uniformVarsPatch = DataPatch{fh.size() + 4, 0};
    write<uint32_t>(fh, static_cast<uint32_t>(gsh.uniformVarCount));
    write<uint32_t>(fh, 0); // vsh.uniformVars.data

    auto initialValuesPatch = DataPatch{fh.size() + 4, 0};
    write<uint32_t>(fh, static_cast<uint32_t>(gsh.initialValueCount));
    write<uint32_t>(fh, 0); // vsh.initialValues.data

    auto loopVarsPatch = DataPatch{fh.size() + 4, 0};
    write<uint32_t>(fh, static_cast<uint32_t>(gsh.loopVarCount));
    write<uint32_t>(fh, 0); // vsh.loopVars.data

    auto samplerVarsPatch = DataPatch{fh.size() + 4, 0};
    write<uint32_t>(fh, static_cast<uint32_t>(gsh.samplerVarCount));
    write<uint32_t>(fh, 0); // vsh.samplerVars.data

    write<uint32_t>(fh, gsh.ringItemSize);
    write<uint32_t>(fh, gsh.hasStreamOut ? 1 : 0);

    for (auto &stride : gsh.streamOutStride)
    {
        write<uint32_t>(fh, stride);
    }

    writeGX2RBuffer(fh, gsh.gx2rBuffer);
    throw std::runtime_error("writeGeometryShader can't implement converting the GX2GeometryShader to a binary file yet.");
    // writeGX2RBuffer(fh, gsh.gx2rVertexShaderData);

    // Now write relocated data
    writeUniformBlocksData(fh, uniformBlocksPatch, dataPatches, textPatches, std::vector<GX2UniformBlock>(gsh.uniformBlocks, gsh.uniformBlocks + gsh.uniformBlockCount));
    writeUniformVarsData(fh, uniformVarsPatch, dataPatches, textPatches, std::vector<GX2UniformVar>(gsh.uniformVars, gsh.uniformVars + gsh.uniformVarCount));
    writeInitialValuesData(fh, initialValuesPatch, dataPatches, textPatches, std::vector<GX2UniformInitialValue>(gsh.initialValues, gsh.initialValues + gsh.initialValueCount));
    writeLoopVarsData(fh, loopVarsPatch, dataPatches, textPatches, std::vector<GX2LoopVar>(gsh.loopVars, gsh.loopVars + gsh.loopVarCount));
    writeSamplerVarsData(fh, samplerVarsPatch, dataPatches, textPatches, std::vector<GX2SamplerVar>(gsh.samplerVars, gsh.samplerVars + gsh.samplerVarCount));
    writeRelocationData(fh, dataPatches, textPatches);
    return true;
}

static bool writeTexture(MemoryFile &fh, const GX2Texture &texture)
{
    write<uint32_t>(fh, static_cast<uint32_t>(texture.surface.dim));
    write<uint32_t>(fh, texture.surface.width);
    write<uint32_t>(fh, texture.surface.height);
    write<uint32_t>(fh, texture.surface.depth);
    write<uint32_t>(fh, texture.surface.mipLevels);
    write<uint32_t>(fh, static_cast<uint32_t>(texture.surface.format));
    write<uint32_t>(fh, static_cast<uint32_t>(texture.surface.aa));
    write<uint32_t>(fh, static_cast<uint32_t>(texture.surface.use));

    write<uint32_t>(fh, static_cast<uint32_t>(texture.surface.imageSize));
    write<uint32_t>(fh, 0); // texture.surface.image

    write<uint32_t>(fh, static_cast<uint32_t>(texture.surface.mipmapSize));
    write<uint32_t>(fh, 0); // texture.surface.mipmap

    write<uint32_t>(fh, static_cast<uint32_t>(texture.surface.tileMode));
    write<uint32_t>(fh, texture.surface.swizzle);
    write<uint32_t>(fh, texture.surface.alignment);
    write<uint32_t>(fh, texture.surface.pitch);

    for (auto &mipLevelOffset : texture.surface.mipLevelOffset)
    {
        write<uint32_t>(fh, mipLevelOffset);
    }

    write<uint32_t>(fh, texture.viewFirstMip);
    write<uint32_t>(fh, texture.viewNumMips);
    write<uint32_t>(fh, texture.viewFirstSlice);
    write<uint32_t>(fh, texture.viewNumSlices);
    write<uint32_t>(fh, texture.compMap);

    write<uint32_t>(fh, texture.regs[0]);
    write<uint32_t>(fh, texture.regs[1]);
    write<uint32_t>(fh, texture.regs[2]);
    write<uint32_t>(fh, texture.regs[3]);
    write<uint32_t>(fh, texture.regs[4]);
    return true;
}

static bool writeBlock(MemoryFile &fh, const GFDBlockHeader &header, const std::vector<uint8_t> &data)
{
    write<uint32_t>(fh, GFDBlockHeader::Magic);
    write<uint32_t>(fh, GFDBlockHeader::HeaderSize);
    write<uint32_t>(fh, header.majorVersion);
    write<uint32_t>(fh, header.minorVersion);
    write<uint32_t>(fh, static_cast<uint32_t>(header.type));
    write<uint32_t>(fh, static_cast<uint32_t>(data.size()));
    write<uint32_t>(fh, header.id);
    write<uint32_t>(fh, header.index);
    writeBinary(fh, data);
    return true;
}

static bool alignNextBlock(MemoryFile &fh, uint32_t &blockID)
{
    auto paddingSize = (0x200 - ((fh.size() + sizeof(GFDBlockHeader)) & 0x1FF)) & 0x1FF;

    if (paddingSize)
    {
        if (paddingSize < sizeof(GFDBlockHeader))
        {
            paddingSize += 0x200;
        }

        paddingSize -= sizeof(GFDBlockHeader);

        GFDBlockHeader paddingHeader;
        paddingHeader.majorVersion = GFDBlockMajorVersion;
        paddingHeader.minorVersion = 0;
        paddingHeader.type = GFDBlockType::Padding;
        paddingHeader.id = blockID++;
        paddingHeader.index = 0;

        writeBlock(fh, paddingHeader, std::vector<uint8_t>(paddingSize, 0));
    }

    return true;
}

bool writeFile(const GFDFile &file, const std::string &path, bool align)
{
    MemoryFile fh;
    auto blockID = uint32_t{0};

    // Write File Header
    write<uint32_t>(fh, GFDFileHeader::Magic);
    write<uint32_t>(fh, GFDFileHeader::HeaderSize);
    write<uint32_t>(fh, GFDFileMajorVersion);
    write<uint32_t>(fh, GFDFileMinorVersion);
    write<uint32_t>(fh, GFDFileGpuVersion);
    write<uint32_t>(fh, align ? 1 : 0); // align
    write<uint32_t>(fh, 0);             // unk1
    write<uint32_t>(fh, 0);             // unk2

    // Write vertex shaders
    for (auto i = 0u; i < file.vertexShaders.size(); ++i)
    {
        std::vector<uint8_t> vertexShaderHeader;
        writeVertexShader(vertexShaderHeader, file.vertexShaders[i]);

        GFDBlockHeader vshHeader;
        vshHeader.majorVersion = GFDBlockMajorVersion;
        vshHeader.minorVersion = 0;
        vshHeader.type = GFDBlockType::VertexShaderHeader;
        vshHeader.id = blockID++;
        vshHeader.index = i;
        writeBlock(fh, vshHeader, vertexShaderHeader);

        if (align)
        {
            alignNextBlock(fh, blockID);
        }

        GFDBlockHeader dataHeader;
        dataHeader.majorVersion = GFDBlockMajorVersion;
        dataHeader.minorVersion = 0;
        dataHeader.type = GFDBlockType::VertexShaderProgram;
        dataHeader.id = blockID++;
        dataHeader.index = i;

    
        const uint32_t size = file.vertexShaders[i].size;
        uint8_t* program = (uint8_t*)file.vertexShaders[i].program;
        const std::vector<uint8_t> shaderData(program, program + size);
        writeBlock(fh, dataHeader, shaderData);
    }

    // Write pixel shaders
    for (auto i = 0u; i < file.pixelShaders.size(); ++i)
    {
        std::vector<uint8_t> pixelShaderHeader;
        writePixelShader(pixelShaderHeader, file.pixelShaders[i]);

        GFDBlockHeader pshHeader;
        pshHeader.majorVersion = GFDBlockMajorVersion;
        pshHeader.minorVersion = 0;
        pshHeader.type = GFDBlockType::PixelShaderHeader;
        pshHeader.id = blockID++;
        pshHeader.index = i;
        writeBlock(fh, pshHeader, pixelShaderHeader);

        if (align)
        {
            alignNextBlock(fh, blockID);
        }

        GFDBlockHeader dataHeader;
        dataHeader.majorVersion = GFDBlockMajorVersion;
        dataHeader.minorVersion = 0;
        dataHeader.type = GFDBlockType::PixelShaderProgram;
        dataHeader.id = blockID++;
        dataHeader.index = i;

        const uint32_t size = file.pixelShaders[i].size;
        uint8_t* program = (uint8_t*)file.pixelShaders[i].program;
        const std::vector<uint8_t> shaderData(program, program + size);
        writeBlock(fh, dataHeader, shaderData);
    }

    // Write geometry shaders
    for (auto i = 0u; i < file.geometryShaders.size(); ++i)
    {
        std::vector<uint8_t> geometryShaderHeader;
        writeGeometryShader(geometryShaderHeader, file.geometryShaders[i]);

        GFDBlockHeader gshHeader;
        gshHeader.majorVersion = GFDBlockMajorVersion;
        gshHeader.minorVersion = 0;
        gshHeader.type = GFDBlockType::GeometryShaderHeader;
        gshHeader.id = blockID++;
        gshHeader.index = i;
        writeBlock(fh, gshHeader, geometryShaderHeader);

        if (align)
        {
            alignNextBlock(fh, blockID);
        }

        if (file.geometryShaders[i].size)
        {
            GFDBlockHeader dataHeader;
            dataHeader.majorVersion = GFDBlockMajorVersion;
            dataHeader.minorVersion = 0;
            dataHeader.type = GFDBlockType::GeometryShaderProgram;
            dataHeader.id = blockID++;
            dataHeader.index = i;

            const uint32_t size = file.geometryShaders[i].size;
            uint8_t* program = (uint8_t*)file.geometryShaders[i].program;
            const std::vector<uint8_t> shaderData(program, program + size);
            writeBlock(fh, dataHeader, shaderData);
        }

        if (align)
        {
            alignNextBlock(fh, blockID);
        }

        // if (file.geometryShaders[i].vertexShaderData.size())
        // {
        //     GFDBlockHeader dataHeader;
        //     dataHeader.majorVersion = GFDBlockMajorVersion;
        //     dataHeader.minorVersion = 0;
        //     dataHeader.type = GFDBlockType::GeometryShaderCopyProgram;
        //     dataHeader.id = blockID++;
        //     dataHeader.index = i;
        //     writeBlock(fh, dataHeader, file.geometryShaders[i].vertexShaderData);
        // }
    }

    // Write textures
    for (auto i = 0u; i < file.textures.size(); ++i)
    {
        std::vector<uint8_t> textureHeader;
        writeTexture(textureHeader, file.textures[i]);

        GFDBlockHeader texHeader;
        texHeader.majorVersion = GFDBlockMajorVersion;
        texHeader.minorVersion = 0;
        texHeader.type = GFDBlockType::TextureHeader;
        texHeader.id = blockID++;
        texHeader.index = i;
        writeBlock(fh, texHeader, textureHeader);

        if (align)
        {
            alignNextBlock(fh, blockID);
        }

        GFDBlockHeader imageHeader;
        imageHeader.majorVersion = GFDBlockMajorVersion;
        imageHeader.minorVersion = 0;
        imageHeader.type = GFDBlockType::TextureImage;
        imageHeader.id = blockID++;
        imageHeader.index = i;

        const uint32_t size = file.textures[i].surface.imageSize;
        uint8_t* image = (uint8_t*)file.textures[i].surface.image;
        const std::vector<uint8_t> imageData(image, image + size);
        writeBlock(fh, imageHeader, imageData);

        if (align)
        {
            alignNextBlock(fh, blockID);
        }

        if (file.textures[i].surface.mipmapSize)
        {
            GFDBlockHeader mipmapHeader;
            mipmapHeader.majorVersion = GFDBlockMajorVersion;
            mipmapHeader.minorVersion = 0;
            mipmapHeader.type = GFDBlockType::TextureImage;
            mipmapHeader.id = blockID++;
            mipmapHeader.index = i;

            const uint32_t size = file.textures[i].surface.mipmapSize;
            uint8_t* mipmap = (uint8_t*)file.textures[i].surface.mipmaps;
            const std::vector<uint8_t> mipmapData(mipmap, mipmap + size);
            writeBlock(fh, mipmapHeader, mipmapData);
        }
    }

    // Write EOF
    GFDBlockHeader eofHeader;
    eofHeader.majorVersion = GFDBlockMajorVersion;
    eofHeader.minorVersion = 0;
    eofHeader.type = GFDBlockType::EndOfFile;
    eofHeader.id = blockID++;
    eofHeader.index = 0;
    writeBlock(fh, eofHeader, {});

    std::ofstream out{path, std::ofstream::binary};
    out.write(reinterpret_cast<const char *>(fh.data()), fh.size());
    return true;
}
