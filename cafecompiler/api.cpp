#include <cstdlib>
#include <cstdio>
#include <cassert>
#include <cstring>

#include "cafe_glsl_compiler.h"

static char s_compilerVersion[] = "v0.2.0";

#if defined(__WUT__)
#include <gx2/shaders.h>
#include <gx2/enum.h>
#include <gx2/mem.h>
#include <coreinit/dynload.h>
#else
#include "gx2_definitions.h"
#define GX2Invalidate(...)
#endif

void DebugLog(const char *format, ...);
size_t _strlcpy(char *dst, const char *src, size_t size);

CafeGLSLCompiler *s_compiler{};
int s_compilerRefCount{0};

void _InitGLSLCompiler()
{
    s_compiler = new CafeGLSLCompiler();
    s_compilerRefCount++;
}

void _DestroyGLSLCompiler()
{
    assert(s_compilerRefCount > 0);
    s_compilerRefCount--;
    if (s_compilerRefCount != 0)
        return;
    delete s_compiler;
    s_compiler = nullptr;
}

#if defined(__WUT__)

static_assert(sizeof(CafeGLSLCompiler::VSRegs) == sizeof(GX2VertexShader::regs));
static_assert(offsetof(CafeGLSLCompiler::VSRegs, sq_pgm_resources_vs) == offsetof(GX2VertexShader, regs.sq_pgm_resources_vs));
static_assert(offsetof(CafeGLSLCompiler::VSRegs, vgt_primitiveid_en) == offsetof(GX2VertexShader, regs.vgt_primitiveid_en));
static_assert(offsetof(CafeGLSLCompiler::VSRegs, spi_vs_out_config) == offsetof(GX2VertexShader, regs.spi_vs_out_config));
static_assert(offsetof(CafeGLSLCompiler::VSRegs, spi_vs_out_id) == offsetof(GX2VertexShader, regs.spi_vs_out_id));
static_assert(offsetof(CafeGLSLCompiler::VSRegs, pa_cl_vs_out_cntl) == offsetof(GX2VertexShader, regs.pa_cl_vs_out_cntl));
static_assert(offsetof(CafeGLSLCompiler::VSRegs, sq_vtx_semantic_clear) == offsetof(GX2VertexShader, regs.sq_vtx_semantic_clear));
static_assert(offsetof(CafeGLSLCompiler::VSRegs, num_sq_vtx_semantic) == offsetof(GX2VertexShader, regs.num_sq_vtx_semantic));
static_assert(offsetof(CafeGLSLCompiler::VSRegs, sq_vtx_semantic) == offsetof(GX2VertexShader, regs.sq_vtx_semantic));
static_assert(offsetof(CafeGLSLCompiler::VSRegs, vgt_strmout_buffer_en) == offsetof(GX2VertexShader, regs.vgt_strmout_buffer_en));
static_assert(offsetof(CafeGLSLCompiler::VSRegs, vgt_vertex_reuse_block_cntl) == offsetof(GX2VertexShader, regs.vgt_vertex_reuse_block_cntl));
static_assert(offsetof(CafeGLSLCompiler::VSRegs, vgt_hos_reuse_depth) == offsetof(GX2VertexShader, regs.vgt_hos_reuse_depth));

static_assert(sizeof(CafeGLSLCompiler::PSRegs) == sizeof(GX2PixelShader::regs));
static_assert(offsetof(CafeGLSLCompiler::PSRegs, sq_pgm_resources_ps) == offsetof(GX2PixelShader, regs.sq_pgm_resources_ps));
static_assert(offsetof(CafeGLSLCompiler::PSRegs, sq_pgm_exports_ps) == offsetof(GX2PixelShader, regs.sq_pgm_exports_ps));
static_assert(offsetof(CafeGLSLCompiler::PSRegs, spi_ps_in_control_0) == offsetof(GX2PixelShader, regs.spi_ps_in_control_0));
static_assert(offsetof(CafeGLSLCompiler::PSRegs, spi_ps_in_control_1) == offsetof(GX2PixelShader, regs.spi_ps_in_control_1));
static_assert(offsetof(CafeGLSLCompiler::PSRegs, num_spi_ps_input_cntl) == offsetof(GX2PixelShader, regs.num_spi_ps_input_cntl));
static_assert(offsetof(CafeGLSLCompiler::PSRegs, spi_ps_input_cntls) == offsetof(GX2PixelShader, regs.spi_ps_input_cntls));
static_assert(offsetof(CafeGLSLCompiler::PSRegs, cb_shader_mask) == offsetof(GX2PixelShader, regs.cb_shader_mask));
static_assert(offsetof(CafeGLSLCompiler::PSRegs, cb_shader_control) == offsetof(GX2PixelShader, regs.cb_shader_control));
static_assert(offsetof(CafeGLSLCompiler::PSRegs, db_shader_control) == offsetof(GX2PixelShader, regs.db_shader_control));
static_assert(offsetof(CafeGLSLCompiler::PSRegs, spi_input_z) == offsetof(GX2PixelShader, regs.spi_input_z));

#endif

bool _CompileShader(const char* shaderSource, CafeGLSLCompiler::SHADER_TYPE shaderType, char* infoLogOut, int infoLogMaxLength)
{
    if (!s_compiler->CompileGLSL(shaderSource, shaderType, infoLogOut, infoLogMaxLength))
    {
        s_compiler->CleanupCurrentProgram();
        return false;
    }
    return true;
}

GX2VertexShader* _CompileVertexShader(const char* shaderSource, char* infoLogOut, int infoLogMaxLength, GLSL_COMPILER_FLAG flags)
{
    if(!_CompileShader(shaderSource, CafeGLSLCompiler::SHADER_TYPE::VERTEX_SHADER, infoLogOut, infoLogMaxLength))
        return nullptr;
    GX2VertexShader* vs = (GX2VertexShader*)malloc(sizeof(GX2VertexShader));
    memset(vs, 0, sizeof(GX2VertexShader));
    // init program data
    uint32_t* programPtr;
    uint32_t programSize;
    s_compiler->GetShaderBytecode(programPtr, programSize);
    vs->program = aligned_alloc(0x100, programSize);
    memcpy(vs->program, programPtr, programSize);
    vs->size = programSize;
#ifdef __WUT__
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, vs->program, vs->size);
#endif
    // set mode
    vs->mode = GX2_SHADER_MODE_UNIFORM_BLOCK;
    // set regs
    CafeGLSLCompiler::VSRegs vsRegs;
    s_compiler->GetVertexShaderRegs(vsRegs);
    memcpy(&vs->regs, &vsRegs, sizeof(vsRegs));
    // set vars
    s_compiler->GetVertexShaderVars(vs);
    // get disassembly if requested
    fprintf(stderr, "Shader compile flags: %08x\n", (unsigned int)flags);
    if(flags & GLSL_COMPILER_FLAG_GENERATE_DISASSEMBLY)
    {
        s_compiler->PrintShaderDisassembly();
    }
    // clean up
    s_compiler->CleanupCurrentProgram();
    // DEBUG
    /*
    DebugLog("_CompileVertexShader debug printing regs:");
    for(int i=0; i<sizeof(CafeGLSLCompiler::VSRegs)/4; i++)
    {
		DebugLog("0x%02x: %08x", i*4, ((unsigned int*)&vs->regs)[i]);		
	}
     */
    return vs;
}

GX2PixelShader* _CompilePixelShader(const char* shaderSource, char* infoLogOut, int infoLogMaxLength, GLSL_COMPILER_FLAG flags)
{
    if(!_CompileShader(shaderSource, CafeGLSLCompiler::SHADER_TYPE::PIXEL_SHADER, infoLogOut, infoLogMaxLength))
        return nullptr;
    GX2PixelShader* ps = (GX2PixelShader*)malloc(sizeof(GX2PixelShader));
    memset(ps, 0, sizeof(GX2PixelShader));
    // init program data
    uint32_t* programPtr;
    uint32_t programSize;
    s_compiler->GetShaderBytecode(programPtr, programSize);
    ps->program = aligned_alloc(0x100, programSize);
    memcpy(ps->program, programPtr, programSize);
    ps->size = programSize;
#ifdef __WUT__
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, ps->program, ps->size);
#endif
    ps->mode = GX2_SHADER_MODE_UNIFORM_BLOCK;
    // set regs
    CafeGLSLCompiler::PSRegs psRegs;
    s_compiler->GetPixelShaderRegs(psRegs);
    memcpy(&ps->regs, &psRegs, sizeof(psRegs));
    // set vars (uniform locations etc)
    s_compiler->GetPixelShaderVars(ps);
    // get disassembly if requested
    if(flags & GLSL_COMPILER_FLAG_GENERATE_DISASSEMBLY)
    {
        s_compiler->PrintShaderDisassembly();
    }
    // clean up
    s_compiler->CleanupCurrentProgram();
    // DEBUG
    /*
    DebugLog("_CompilePixelShader debug printing regs:");
    for(int i=0; i<sizeof(CafeGLSLCompiler::PSRegs)/4; i++)
    {
		DebugLog("0x%02x: %08x", i*4, ((unsigned int*)&ps->regs)[i]);		
	}
     */
    return ps;
}

void _FreeVertexShader(GX2VertexShader* shader)
{
    for(uint32_t i=0; i<shader->uniformBlockCount; i++)
        free((void*)shader->uniformBlocks[i].name);
    free(shader->uniformBlocks);
    for(uint32_t i=0; i<shader->uniformVarCount; i++)
        free((void*)shader->uniformVars[i].name);
    free(shader->uniformVars);
    free(shader->initialValues);
    free(shader->loopVars);
    for(uint32_t i=0; i<shader->samplerVarCount; i++)
        free((void*)shader->samplerVars[i].name);
    free(shader->samplerVars);
    for(uint32_t i=0; i<shader->attribVarCount; i++)
        free((void*)shader->attribVars[i].name);
    free(shader->attribVars);
    free(shader->program);
    free(shader);
}

void _FreePixelShader(GX2PixelShader* shader)
{
    for(uint32_t i=0; i<shader->uniformBlockCount; i++)
        free((void*)shader->uniformBlocks[i].name);
    free(shader->uniformBlocks);
    for(uint32_t i=0; i<shader->uniformVarCount; i++)
        free((void*)shader->uniformVars[i].name);
    free(shader->uniformVars);
    free(shader->initialValues);
    free(shader->loopVars);
    for(uint32_t i=0; i<shader->samplerVarCount; i++)
        free((void*)shader->samplerVars[i].name);
    free(shader->samplerVars);
    free(shader->program);
    free(shader);
}

void TestCompiler();

#define API_EXPORT     __attribute__ ((__used__)) __attribute__ ((visibility ("default")))

extern "C"
{
    API_EXPORT void InitGLSLCompiler()
    {
        _InitGLSLCompiler();
    }

    API_EXPORT void DestroyGLSLCompiler()
    {
        _DestroyGLSLCompiler();
    }

    API_EXPORT const char* GetGLSLCompilerVersion()
    {
        return s_compilerVersion;
    }

    API_EXPORT GX2VertexShader* CompileVertexShader(const char* shaderSource, char* infoLogOut, int infoLogMaxLength, GLSL_COMPILER_FLAG flags)
    {
       return _CompileVertexShader(shaderSource, infoLogOut, infoLogMaxLength, flags);
    }

    API_EXPORT GX2PixelShader* CompilePixelShader(const char* shaderSource, char* infoLogOut, int infoLogMaxLength, GLSL_COMPILER_FLAG flags)
    {
       return _CompilePixelShader(shaderSource, infoLogOut, infoLogMaxLength, flags);
    }

    API_EXPORT void FreeVertexShader(GX2VertexShader* shader)
    {
        _FreeVertexShader(shader);
    }

    API_EXPORT void FreePixelShader(GX2PixelShader* shader)
    {
        _FreePixelShader(shader);
    }

#if defined(__WUT__)
    int rpl_entry(OSDynLoad_Module module, OSDynLoad_EntryReason reason)
    {
        if (reason == 1)
        {
            // load
        }
        else if (reason == 2)
        {
            // unload
            delete s_compiler;
        }
        return 0;
    }
#endif

};


