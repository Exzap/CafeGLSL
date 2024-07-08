#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// struct GFDSurface
// {
//     uint32_t dim;
//     uint32_t width;
//     uint32_t height;
//     uint32_t depth;
//     uint32_t mipLevels;
//     uint32_t format;
//     uint32_t aa;

//     union
//     {
//         uint32_t use;
//         uint32_t resourceFlags;
//     };

//     std::vector<uint8_t> image;
//     std::vector<uint8_t> mipmap;
//     uint32_t tileMode;
//     uint32_t swizzle;
//     uint32_t alignment;
//     uint32_t pitch;
//     std::array<uint32_t, 13> mipLevelOffset;
// };

// struct GFDTexture
// {
//     GFDSurface surface;
//     uint32_t viewFirstMip;
//     uint32_t viewNumMips;
//     uint32_t viewFirstSlice;
//     uint32_t viewNumSlices;
//     uint32_t compMap;

//     struct
//     {
//         uint32_t word0;
//         uint32_t word1;
//         uint32_t word4;
//         uint32_t word5;
//         uint32_t word6;
//     } regs;
// };

// struct GFDFetchShader
// {
//     uint32_t type;

//     struct
//     {
//         uint32_t sq_pgm_resources_fs;
//     } regs;

//     uint32_t size;
//     uint8_t *data;
//     uint32_t attribCount;
//     uint32_t numDivisors;
//     uint32_t divisors[2];
// };

// struct GFDUniformVar
// {
//     std::string name;
//     uint32_t type;
//     uint32_t count;
//     uint32_t offset;
//     int32_t block;
// };

// struct GFDUniformInitialValue
// {
//     std::array<float, 4> value;
//     uint32_t offset;
// };

// struct GFDUniformBlock
// {
//     std::string name;
//     uint32_t offset;
//     uint32_t size;
// };

// struct GFDAttribVar
// {
//     std::string name;
//     uint32_t type;
//     uint32_t count;
//     uint32_t location;
// };

// struct GFDSamplerVar
// {
//     std::string name;
//     uint32_t type;
//     uint32_t location;
// };

// struct GFDLoopVar
// {
//     uint32_t offset;
//     uint32_t value;
// };

// struct GFDRBuffer
// {
//     uint32_t flags;
//     uint32_t elemSize;
//     uint32_t elemCount;
//     std::vector<uint8_t> buffer;
// };

// struct GFDVertexShader
// {
//     struct
//     {
//         uint32_t sq_pgm_resources_vs;
//         uint32_t vgt_primitiveid_en;
//         uint32_t spi_vs_out_config;
//         uint32_t num_spi_vs_out_id;
//         uint32_t spi_vs_out_id[10];
//         uint32_t pa_cl_vs_out_cntl;
//         uint32_t sq_vtx_semantic_clear;
//         uint32_t num_sq_vtx_semantic;
//         uint32_t sq_vtx_semantic[32];
//         uint32_t vgt_strmout_buffer_en;
//         uint32_t vgt_vertex_reuse_block_cntl;
//         uint32_t vgt_hos_reuse_depth;
//     } regs;

//     std::vector<uint8_t> data;
//     uint32_t mode;
//     std::vector<GFDUniformBlock> uniformBlocks;
//     std::vector<GFDUniformVar> uniformVars;
//     std::vector<GFDUniformInitialValue> initialValues;
//     std::vector<GFDLoopVar> loopVars;
//     std::vector<GFDSamplerVar> samplerVars;
//     std::vector<GFDAttribVar> attribVars;
//     uint32_t ringItemSize;
//     bool hasStreamOut;
//     std::array<uint32_t, 4> streamOutStride;
//     GFDRBuffer gx2rData;
// };

// struct GFDPixelShader
// {
//     struct
//     {
//         uint32_t sq_pgm_resources_ps;
//         uint32_t sq_pgm_exports_ps;
//         uint32_t spi_ps_in_control_0;
//         uint32_t spi_ps_in_control_1;
//         uint32_t num_spi_ps_input_cntl;
//         uint32_t spi_ps_input_cntls[32];
//         uint32_t cb_shader_mask;
//         uint32_t cb_shader_control;
//         uint32_t db_shader_control;
//         uint32_t spi_input_z;
//     } regs;

//     std::vector<uint8_t> data;
//     uint32_t mode;
//     std::vector<GFDUniformBlock> uniformBlocks;
//     std::vector<GFDUniformVar> uniformVars;
//     std::vector<GFDUniformInitialValue> initialValues;
//     std::vector<GFDLoopVar> loopVars;
//     std::vector<GFDSamplerVar> samplerVars;
//     GFDRBuffer gx2rData;
// };

// struct GFDGeometryShader
// {
//     struct
//     {
//         uint32_t sq_pgm_resources_gs;
//         uint32_t vgt_gs_out_prim_type;
//         uint32_t vgt_gs_mode;
//         uint32_t pa_cl_vs_out_cntl;
//         uint32_t sq_pgm_resources_vs;
//         uint32_t sq_gs_vert_itemsize;
//         uint32_t spi_vs_out_config;
//         uint32_t num_spi_vs_out_id;
//         uint32_t spi_vs_out_id[10];
//         uint32_t vgt_strmout_buffer_en;
//     } regs;

//     std::vector<uint8_t> data;
//     std::vector<uint8_t> vertexShaderData;
//     uint32_t mode;
//     std::vector<GFDUniformBlock> uniformBlocks;
//     std::vector<GFDUniformVar> uniformVars;
//     std::vector<GFDUniformInitialValue> initialValues;
//     std::vector<GFDLoopVar> loopVars;
//     std::vector<GFDSamplerVar> samplerVars;
//     uint32_t ringItemSize;
//     bool hasStreamOut;
//     std::array<uint32_t, 4> streamOutStride;
//     GFDRBuffer gx2rData;
//     GFDRBuffer gx2rVertexShaderData;
// };
