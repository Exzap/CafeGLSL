#include "gx2_definitions.h"
#include "cafe_glsl_compiler.h" // internal

#include "CafeGLSLCompiler.h" // the public header

#include <cstdlib>
#include <cassert>
#include <cstring>

void DebugLog(const char *format, ...);

GX2PixelShader* TestCompilePS(const char* shaderSource)
{
    char infoLogBuffer[1024];
    GX2PixelShader* ps = GLSL_CompilePixelShader(shaderSource, infoLogBuffer, 1024, GLSL_COMPILER_FLAG_GENERATE_DISASSEMBLY);
    if(!ps)
    {
        DebugLog("Shader failed to compile. Source:\n");
        DebugLog("%s", shaderSource);
        DebugLog("Info log:\n");
        DebugLog("%s", infoLogBuffer);
        exit(-2);
    }

    // debug regs
    for(int i=0; i<32; i++)
    {
        DebugLog("spi_ps_input_cntls[%2d]: %08x", i, ps->regs.spi_ps_input_cntls[i]);
    }

    return ps;
}

template<typename T>
int32_t GX2Shader_GetTextureSamplerLocation(T* shader, const char* name)
{
    for (uint32_t i = 0; i < shader->samplerVarCount; i++)
    {
        if(strcmp(shader->samplerVars[i].name, name) == 0)
            return shader->samplerVars[i].location;
    }
    return -1;
}

template<typename T>
int32_t GX2Shader_GetUniformBlockLocation(T* shader, const char* name)
{
    for (uint32_t i = 0; i < shader->uniformBlockCount; i++)
    {
        if(strcmp(shader->uniformBlocks[i].name, name) == 0)
            return shader->uniformBlocks[i].offset;
    }
    return -1;
}

template<typename T>
void PrintShaderVars(T* shader)
{
    DebugLog("------- Shader program IO -------");
    DebugLog("Uniform blocks:");
    for (uint32_t i = 0; i<shader->uniformBlockCount; i++)
    {
        DebugLog("UBO %s location: %d", shader->uniformBlocks[i].name, shader->uniformBlocks[i].offset);
    }
    DebugLog("Samplers:");
    for(uint32_t i=0; i<shader->samplerVarCount; i++)
    {
        DebugLog("Sampler %s location %d", shader->samplerVars[i].name, shader->samplerVars[i].location);
    }
}

void TestShader1()
{
    const char* psSrc = R"(
#version 450
layout(binding = 2) uniform sampler2D textureSampler;
layout(binding = 4) uniform sampler2D textureSampler2;

layout(binding = 9) uniform uf_data9
{
	float uf_time;
    float uf_unused;
};

layout(binding = 11) uniform uf_data11
{
	vec4 temp;
    float uf_temp2;
};

layout(location = 0) in vec2 textureCoord;
layout(location = 5) in vec2 textureCoord_at5;
layout(location = 0) out vec4 outputColor;
layout(location = 3) out vec4 outputColor3;
void main()
{
  vec4 textureColor = texture(textureSampler, textureCoord) + texture(textureSampler2, textureCoord_at5);
  outputColor = textureColor + vec4(uf_time, uf_temp2, 0.0, 0.0);
  outputColor3 = outputColor * 2.0;
}
        )";
    GX2PixelShader* ps = TestCompilePS(psSrc);
    assert(ps->uniformBlockCount == 2);
    assert(ps->samplerVarCount == 2);
    assert(GX2Shader_GetUniformBlockLocation(ps, "uf_data9") == 9);
    assert(GX2Shader_GetUniformBlockLocation(ps, "uf_data11") == 11);
    assert(GX2Shader_GetTextureSamplerLocation(ps, "textureSampler") == 2);
    assert(GX2Shader_GetTextureSamplerLocation(ps, "textureSampler2") == 4);
}

void TestShader2()
{
    const char* psSrc = R"(
#version 150
#extension GL_ARB_separate_shader_objects : require
uniform sampler2D Texture;
uniform float uf_test0;
uniform float uf_test1;
uniform float uf_test2;
uniform float uf_test3;
uniform float uf_test4;
uniform mat4 uf_test5Mat;
uniform float uf_test6;
uniform mat4 uf_test6MatArray[3];
uniform vec4 uf_test7;
in vec2 Frag_UV;
//in vec4 Frag_Color;
in vec4 Frag_ColorA;
layout(location = 10) in vec4 Frag_ColorB;
in vec4 Frag_ColorC;
out vec4 Out_Color;
void main()
{
    Out_Color = (Frag_ColorA + Frag_ColorB + Frag_ColorC) * texture(Texture, Frag_UV.st);
    Out_Color.r += uf_test0 + uf_test1 + uf_test2 + uf_test3 + uf_test4 + uf_test5Mat[0].x + uf_test6 + uf_test6MatArray[0][0].x + uf_test6MatArray[1][0].x + uf_test6MatArray[2][0].x + uf_test7.y;
}
)";
    GX2PixelShader* ps = TestCompilePS(psSrc);
    PrintShaderVars(ps);
    //assert(ps->uniformBlockCount == 2);
    //assert(ps->samplerVarCount == 2);
    //assert(GX2Shader_GetProgramInputLocation(ps, "Frag_UV") == 0);
    //assert(GX2Shader_GetProgramInputLocation(ps, "Frag_Color") == 1);
    //assert(GX2Shader_GetProgramInputLocation(ps, "textureCoord_at5") == 5);
    //assert(GX2Shader_GetProgramOutputLocation(ps, "Out_Color") == 0);
    //assert(GX2Shader_GetNumProgramInputs(ps) == 2);
    //assert(GX2Shader_GetNumProgramOutputs(ps) == 1);
    assert(GX2Shader_GetTextureSamplerLocation(ps, "Texture") == 0);

}

int RunTests()
{
    DebugLog("Initialize compiler...\n");
    if (!GLSL_Init())
    {
        DebugLog("Failed\n");
        return -1;
    }
    DebugLog("Running compiler tests...\n");

    TestShader1();
    TestShader2();

    DebugLog("Done!");
    GLSL_Shutdown();
    return 0;
}
