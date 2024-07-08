#pragma once

#include <vector>
#include <array>

#ifdef __WUT__
// if available use header for shader structs
#include <gx2/surface.h>
#include <gx2/texture.h>
#include <gx2/shaders.h>
#else
// when compiling without Cafe headers we manually define the structs so that code can be kept simple
// note that the memory layout and endianness won't be the same
#include <stdint.h>
typedef int32_t BOOL;

typedef enum GX2FetchShaderType
{
    GX2_FETCH_SHADER_TESSELLATION_NONE     = 0,
    GX2_FETCH_SHADER_TESSELLATION_LINE     = 1,
    GX2_FETCH_SHADER_TESSELLATION_TRIANGLE = 2,
    GX2_FETCH_SHADER_TESSELLATION_QUAD     = 3
}GX2FetchShaderType;

typedef enum GX2ShaderMode
{
    GX2_SHADER_MODE_UNIFORM_REGISTER       = 0,
    GX2_SHADER_MODE_UNIFORM_BLOCK          = 1,
    GX2_SHADER_MODE_GEOMETRY_SHADER        = 2,
    GX2_SHADER_MODE_COMPUTE_SHADER         = 3
}GX2ShaderMode;

typedef enum GX2ShaderVarType
{
    GX2_SHADER_VAR_TYPE_VOID               = 0,
    GX2_SHADER_VAR_TYPE_BOOL               = 1,
    GX2_SHADER_VAR_TYPE_INT                = 2,
    GX2_SHADER_VAR_TYPE_UINT               = 3,
    GX2_SHADER_VAR_TYPE_FLOAT              = 4,
    GX2_SHADER_VAR_TYPE_DOUBLE             = 5,
    GX2_SHADER_VAR_TYPE_DOUBLE2            = 6,
    GX2_SHADER_VAR_TYPE_DOUBLE3            = 7,
    GX2_SHADER_VAR_TYPE_DOUBLE4            = 8,
    GX2_SHADER_VAR_TYPE_FLOAT2             = 9,
    GX2_SHADER_VAR_TYPE_FLOAT3             = 10,
    GX2_SHADER_VAR_TYPE_FLOAT4             = 11,
    GX2_SHADER_VAR_TYPE_BOOL2              = 12,
    GX2_SHADER_VAR_TYPE_BOOL3              = 13,
    GX2_SHADER_VAR_TYPE_BOOL4              = 14,
    GX2_SHADER_VAR_TYPE_INT2               = 15,
    GX2_SHADER_VAR_TYPE_INT3               = 16,
    GX2_SHADER_VAR_TYPE_INT4               = 17,
    GX2_SHADER_VAR_TYPE_UINT2              = 18,
    GX2_SHADER_VAR_TYPE_UINT3              = 19,
    GX2_SHADER_VAR_TYPE_UINT4              = 20,
    GX2_SHADER_VAR_TYPE_FLOAT2X2           = 21,
    GX2_SHADER_VAR_TYPE_FLOAT2X3           = 22,
    GX2_SHADER_VAR_TYPE_FLOAT2X4           = 23,
    GX2_SHADER_VAR_TYPE_FLOAT3X2           = 24,
    GX2_SHADER_VAR_TYPE_FLOAT3X3           = 25,
    GX2_SHADER_VAR_TYPE_FLOAT3X4           = 26,
    GX2_SHADER_VAR_TYPE_FLOAT4X2           = 27,
    GX2_SHADER_VAR_TYPE_FLOAT4X3           = 28,
    GX2_SHADER_VAR_TYPE_FLOAT4X4           = 29,
    GX2_SHADER_VAR_TYPE_DOUBLE2X2          = 30,
    GX2_SHADER_VAR_TYPE_DOUBLE2X3          = 31,
    GX2_SHADER_VAR_TYPE_DOUBLE2X4          = 32,
    GX2_SHADER_VAR_TYPE_DOUBLE3X2          = 33,
    GX2_SHADER_VAR_TYPE_DOUBLE3X3          = 34,
    GX2_SHADER_VAR_TYPE_DOUBLE3X4          = 35,
    GX2_SHADER_VAR_TYPE_DOUBLE4X2          = 36,
    GX2_SHADER_VAR_TYPE_DOUBLE4X3          = 37,
    GX2_SHADER_VAR_TYPE_DOUBLE4X4          = 38
}GX2ShaderVarType;

typedef enum GX2SamplerVarType
{
    GX2_SAMPLER_VAR_TYPE_SAMPLER_1D        = 0,
    GX2_SAMPLER_VAR_TYPE_SAMPLER_2D        = 1,
    GX2_SAMPLER_VAR_TYPE_SAMPLER_3D        = 3,
    GX2_SAMPLER_VAR_TYPE_SAMPLER_CUBE      = 4
}GX2SamplerVarType;

typedef enum GX2RResourceFlags
{

}GX2RResourceFlags;

struct GX2RBuffer
{
    GX2RResourceFlags flags;
    uint32_t elemSize;
    uint32_t elemCount;
    void *buffer;
};

struct GX2FetchShader
{
    GX2FetchShaderType type;
    struct
    {
        uint32_t sq_pgm_resources_fs;
    }regs;
    uint32_t size;
    void* program;
    uint32_t attribCount;
    uint32_t numDivisors;
    uint32_t divisors[2];
};

struct GX2UniformBlock
{
    const char* name;
    uint32_t offset;
    uint32_t size;
};

struct GX2UniformVar
{
    const char* name;
    GX2ShaderVarType type;
    uint32_t count;
    uint32_t offset;
    int32_t block;
};

struct GX2UniformInitialValue
{
    float value[4];
    uint32_t offset;
};

struct GX2LoopVar
{
    uint32_t offset;
    uint32_t value;
};

struct GX2SamplerVar
{
    const char* name;
    GX2SamplerVarType type;
    uint32_t location;
};

struct GX2AttribVar
{
    const char *name;
    GX2ShaderVarType type;
    uint32_t count;
    uint32_t location;
};

struct GX2VertexShader
{
    struct
    {
        uint32_t sq_pgm_resources_vs;
        uint32_t vgt_primitiveid_en;
        uint32_t spi_vs_out_config;
        uint32_t num_spi_vs_out_id;
        uint32_t spi_vs_out_id[10];
        uint32_t pa_cl_vs_out_cntl;
        uint32_t sq_vtx_semantic_clear;
        uint32_t num_sq_vtx_semantic;
        uint32_t sq_vtx_semantic[32];
        uint32_t vgt_strmout_buffer_en;
        uint32_t vgt_vertex_reuse_block_cntl;
        uint32_t vgt_hos_reuse_depth;
    }regs;
    uint32_t size;
    void* program;
    GX2ShaderMode mode;
    uint32_t uniformBlockCount;
    GX2UniformBlock* uniformBlocks;
    uint32_t uniformVarCount;
    GX2UniformVar* uniformVars;
    uint32_t initialValueCount;
    GX2UniformInitialValue* initialValues;
    uint32_t loopVarCount;
    GX2LoopVar* loopVars;
    uint32_t samplerVarCount;
    GX2SamplerVar* samplerVars;
    uint32_t attribVarCount;
    GX2AttribVar* attribVars;
    uint32_t ringItemsize;
    BOOL hasStreamOut;
    uint32_t streamOutStride[4];
    GX2RBuffer gx2rBuffer;
};

struct GX2PixelShader
{
    struct
    {
        uint32_t sq_pgm_resources_ps;
        uint32_t sq_pgm_exports_ps;
        uint32_t spi_ps_in_control_0;
        uint32_t spi_ps_in_control_1;
        uint32_t num_spi_ps_input_cntl;
        uint32_t spi_ps_input_cntls[32];
        uint32_t cb_shader_mask;
        uint32_t cb_shader_control;
        uint32_t db_shader_control;
        uint32_t spi_input_z;
    } regs;
    uint32_t size;
    void* program;
    GX2ShaderMode mode;
    uint32_t uniformBlockCount;
    GX2UniformBlock* uniformBlocks;
    uint32_t uniformVarCount;
    GX2UniformVar* uniformVars;
    uint32_t initialValueCount;
    GX2UniformInitialValue* initialValues;
    uint32_t loopVarCount;
    GX2LoopVar* loopVars;
    uint32_t samplerVarCount;
    GX2SamplerVar* samplerVars;
    GX2RBuffer gx2rBuffer;
};

struct GX2GeometryShader
{
    struct
    {
        uint32_t sq_pgm_resources_gs;
        uint32_t vgt_gs_out_prim_type;
        uint32_t vgt_gs_mode;
        uint32_t pa_cl_vs_out_cntl;
        uint32_t sq_pgm_resources_vs;
        uint32_t sq_gs_vert_itemsize;
        uint32_t spi_vs_out_config;
        uint32_t num_spi_vs_out_id;
        uint32_t spi_vs_out_id[10];
        uint32_t vgt_strmout_buffer_en;
    } regs;

    uint32_t size;
    void *program;
    uint32_t vertexProgramSize;
    void* vertexProgram;
    GX2ShaderMode mode;
    uint32_t uniformBlockCount;
    GX2UniformBlock* uniformBlocks;
    uint32_t uniformVarCount;
    GX2UniformVar* uniformVars;
    uint32_t initialValueCount;
    GX2UniformInitialValue* initialValues;
    uint32_t loopVarCount;
    GX2LoopVar* loopVars;
    uint32_t samplerVarCount;
    GX2SamplerVar* samplerVars;
    uint32_t ringItemSize;
    BOOL hasStreamOut;
    uint32_t streamOutStride[4];
    GX2RBuffer gx2rBuffer;
};

struct GX2Surface
{
    uint32_t dim;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t mipLevels;
    uint32_t format;
    uint32_t aa;

    union
    {
        uint32_t use;
        uint32_t resourceFlags;
    };

    uint32_t imageSize;
    void *image;
    uint32_t mipmapSize;
    void *mipmaps;
    uint32_t tileMode;
    uint32_t swizzle;
    uint32_t alignment;
    uint32_t pitch;
    std::array<uint32_t, 13> mipLevelOffset;
};

struct GX2Texture
{
    GX2Surface surface;
    uint32_t viewFirstMip;
    uint32_t viewNumMips;
    uint32_t viewFirstSlice;
    uint32_t viewNumSlices;
    uint32_t compMap;

    uint32_t regs[5];
};

#endif
