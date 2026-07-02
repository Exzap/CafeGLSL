
#define NO_SFIXED
#define _CAFE_NO_IR

#define _Static_assert static_assert

#include <stdio.h>
#include <stdint.h>
#include <mutex>

#include "util/macros.h"
#include "util/format/u_format.h"
#include "gallium/auxiliary/util/u_range.h"
#include "compiler/nir/nir.h"

// C headers
#include "gallium/drivers/r600/r600_pipe.h"
#include "gallium/drivers/r600/r600_isa.h"
#include "gallium/drivers/r600/r600_shader.h"
#include "gallium/drivers/r600/r600_public.h"
#include "gallium/drivers/r600/r600d.h"
#include "gallium/drivers/r600/sb/sb_public.h"

#include "mesa/state_tracker/st_context.h"
#include "mesa/state_tracker/st_program.h" // for st_variant

#include "compiler/glsl/program.h"
#include "compiler/glsl/ir.h"
#include "compiler/glsl/builtin_functions.h" // _mesa_glsl_builtin_functions_init_or_ref
#include "mesa/program/program.h"
#include "mesa/program/link_program.h"
#include "mesa/main/shaderobj.h"
#include "mesa/main/shaderapi.h" // be careful about calling from this as they may have side effects

#include "gallium/drivers/r600/sfn/sfn_nir.h"

#include "util/strtod.h" // _mesa_locale_init

#include "cafe_glsl_compiler.h"
#include "gx2_definitions.h"
#include "CafeGLSLCompiler.h"

#if defined(__WUT__)
#include <coreinit/debug.h>
#endif

size_t _strlcpy(char *dst, const char *src, size_t size)
{
	char *d = dst;
	const char *s = src;
	size_t n = size;
	if (n != 0)
	{
		while (--n != 0)
		{
			if ((*d++ = *s++) == '\0')
				break;
		}
 	}
	if (n == 0)
	{
		if (size != 0)
			*d = '\0';
		while (*s++);
	}
	return s - src - 1;
}

#define LATTE_FAMILY_CHIP CHIP_RV730
#define LATTE_GFX_LEVEL R700

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
	if (alignment % sizeof(void *) != 0 || (alignment & (alignment - 1)) != 0)
	{
		return EINVAL;
	}

#ifndef _MSC_VER
	void *ptr = aligned_alloc(alignment, size);
	// Alignment _has_ to be a power of 2
#elif !defined(_M_ARM64)
	void *ptr = _aligned_malloc(size, 1ULL << (64 - __lzcnt64(alignment)));
#else
	void *ptr = _aligned_malloc(size, 1ULL << (64 - _CountLeadingZeros64(alignment)));
#endif
	if (ptr == NULL)
	{
		return ENOMEM;
	}

	*memptr = ptr;
	return 0;
}

void DebugLogNoFormat(const char* str)
{
#if defined(__WUT__)
	size_t len = strlen(str);
	OSConsoleWrite(str, len);
	OSConsoleWrite("\n", 1);
#else
	puts(str);
#endif
}

void DebugLog(const char *format, ...)
{
	char buffer[256];
	va_list aptr;
	va_start(aptr, format);
	vsprintf(buffer, format, aptr);
	va_end(aptr);
	DebugLogNoFormat(buffer);
}

CafeGLSLCompiler::CafeGLSLCompiler()
{
	_mesa_glsl_builtin_functions_init_or_ref();
	_InitGLContext();
	_InitScreen();
	_InitStateContext();
}

CafeGLSLCompiler::~CafeGLSLCompiler()
{
	_mesa_glsl_builtin_functions_decref();
	CleanupCurrentProgram();
	r600Screen->b.b.destroy(&r600Screen->b.b); // also deletes r600Ctx and isa?
	r600Screen = nullptr;
	free(this->glCtx);
	free(stContext);
	if(r600Ctx->sb_context)
		r600_sb_context_destroy(r600Ctx->sb_context);
	r600_isa_destroy(r600Ctx->isa);
	free(r600Ctx);
}

pipe_shader_type GetMesaShaderType(CafeGLSLCompiler::SHADER_TYPE shaderType)
{
	switch (shaderType)
	{
	case CafeGLSLCompiler::SHADER_TYPE::VERTEX_SHADER:
		return MESA_SHADER_VERTEX;
	case CafeGLSLCompiler::SHADER_TYPE::GEOMETRY_SHADER:
		return MESA_SHADER_GEOMETRY;
	case CafeGLSLCompiler::SHADER_TYPE::PIXEL_SHADER:
		return MESA_SHADER_FRAGMENT;
	}
	assert(false);
	return MESA_SHADER_VERTEX;
}

bool CafeGLSLCompiler::CompileGLSL(const char *shaderSource, SHADER_TYPE shaderType, char* infoLogOut, int infoLogMaxLength)
{
	CleanupCurrentProgram();
	lastCompiledShaderType = shaderType;
	pipe_shader_type mesaShaderType = GetMesaShaderType(shaderType);
	shProg = _mesa_new_shader_program(0);
	shProg->SeparateShader = true;
	// alloc shader
	gl_shader* shader = _mesa_new_shader(0, mesaShaderType);
	// alloc and append shader
	shProg->Shaders = (struct gl_shader **)malloc(sizeof(gl_shader *) * 1);
	shProg->Shaders[shProg->NumShaders] = shader;
	assert(shader->RefCount == 1);
	shProg->NumShaders = 1;

	shader->Source = strdup(shaderSource);

	/* compile */
	_mesa_clear_shader_program_data(glCtx, shProg); // necessary?
	_mesa_glsl_compile_shader(glCtx, shader, false, false, true);

	if (shader->CompileStatus != COMPILE_SUCCESS)
	{
		// error
		DebugLogNoFormat("Shader failed to compile. Log:");
		DebugLogNoFormat(shader->InfoLog);
		_strlcpy(infoLogOut, shader->InfoLog, infoLogMaxLength);
		CleanupCurrentProgram();
		return false;
	}

	/* link */
	// set some variables necessary for linking
	glCtx->Driver.NewProgram = _mesa_new_program;
	gl_pipeline_object tmpPipelineObj{};
	glCtx->_Shader = &tmpPipelineObj; // mesa reads _Shader->Flags to check for debug flags so we create a dummy object here
	glCtx->Const.ShaderCompilerOptions[MESA_SHADER_VERTEX].NirOptions = &r600Screen->b.nir_options;
	glCtx->Const.ShaderCompilerOptions[MESA_SHADER_GEOMETRY].NirOptions = &r600Screen->b.nir_options;
	glCtx->Const.ShaderCompilerOptions[MESA_SHADER_FRAGMENT].NirOptions = &r600Screen->b.nir_options_fs;

	_mesa_glsl_link_shader(glCtx, shProg);

	if (shProg->data->LinkStatus == LINKING_FAILURE)
	{
		DebugLog("Error linking shader %u. Log:", shProg->Name);
		DebugLogNoFormat(shProg->data->InfoLog);
		_strlcpy(infoLogOut, shProg->data->InfoLog, infoLogMaxLength);
		CleanupCurrentProgram();
		return false;
	}

	return true;
}

static void GetShaderBinary(struct r600_pipe_shader *shader, uint32_t *&programPtr, uint32_t &programSize)
{
	programSize = shader->shader.bc.ndw * 4;
	programPtr = (uint32_t *)align_calloc(programSize, 0x100); // use ExpDefaultHeap alloc here?
	if (R600_BIG_ENDIAN)
	{
		for (uint32_t i = 0; i < shader->shader.bc.ndw; ++i)
			programPtr[i] = util_cpu_to_le32(shader->shader.bc.bytecode[i]);
	}
	else
	{
		memcpy(programPtr, shader->shader.bc.bytecode, programSize);
	}
}

r600_pipe_shader* CafeGLSLCompiler::GetCurrentPipeShader()
{
    assert(shProg->data->LinkStatus == LINKING_SUCCESS);
    gl_linked_shader *shader = nullptr;
    for (unsigned i = 0; i < MESA_SHADER_STAGES; i++)
    {
        if (shProg->_LinkedShaders[i] == NULL)
            continue;
        shader = shProg->_LinkedShaders[i];
    }
    assert(shader);
    //struct st_context *variantCtx = shader->Program->variants->st;
    assert(shader->Program->variants->next == NULL); // we expect only one variant
    struct r600_pipe_shader_selector *sel = (struct r600_pipe_shader_selector *)shader->Program->variants->driver_shader;
    return sel->current;
}

gl_program* CafeGLSLCompiler::GetCurrentGLProgram()
{
    assert(shProg->data->LinkStatus == LINKING_SUCCESS);
    gl_linked_shader *shader = nullptr;
    for (unsigned i = 0; i < MESA_SHADER_STAGES; i++)
    {
        if (shProg->_LinkedShaders[i] == NULL)
            continue;
        shader = shProg->_LinkedShaders[i];
    }
    assert(shader);
    return shader->Program;
}

bool CafeGLSLCompiler::GetShaderBytecode(uint32_t *&programPtr, uint32_t &programSize)
{
	struct r600_pipe_shader *pipeShader = GetCurrentPipeShader();
	GetShaderBinary(pipeShader, programPtr, programSize);
	return true;
}

#ifdef __WUT__
int _stderr_write_callback(struct _reent *r, void *, const char *data, int len)
{
    OSConsoleWrite(data, len);
    return len;
}
#endif

void CafeGLSLCompiler::PrintShaderDisassembly()
{
#ifdef __WUT__
    fflush(stderr);
    auto prevWrite = stderr->_write;
    stderr->_write = _stderr_write_callback;
    fprintf(stderr, "Shader disassembly:\n");
#endif
    struct r600_pipe_shader *pipeShader = GetCurrentPipeShader();
    r600_bytecode_disasm(&pipeShader->shader.bc); // writes disassembly to stderr
#ifdef __WUT__
    fflush(stderr);
    stderr->_write = prevWrite;
#endif
}

gl_context *_GLSLCreateDefaultContext(gl_api api, GLuint GLSLVersion)
{
	struct gl_context *ctx = (struct gl_context *)malloc(sizeof(gl_context));
	memset(ctx, 0, sizeof(*ctx));

    //ctx->Const.ForceGLSLVersion = 450; // same as #version 450 (might also be a bool?)

    ctx->Version = 4.5;
    ctx->Const.GLSLVersion = GLSLVersion;
    ctx->Const.GLSLVersionCompat = GLSLVersion;
	ctx->API = api;

    ctx->Const.AllowGLSLCompatShaders = true;
    ctx->Extensions.Version = ctx->Version;

	ctx->Extensions.dummy_true = true;
	ctx->Extensions.ARB_compute_shader = true;
	ctx->Extensions.ARB_compute_variable_group_size = true;
	ctx->Extensions.ARB_conservative_depth = true;
	ctx->Extensions.ARB_draw_instanced = true;
	ctx->Extensions.ARB_ES2_compatibility = true;
	ctx->Extensions.ARB_ES3_compatibility = true;
	ctx->Extensions.ARB_explicit_attrib_location = true;
    ctx->Extensions.ARB_explicit_uniform_location = true;
    ctx->Extensions.ARB_fragment_coord_conventions = true;
	ctx->Extensions.ARB_fragment_layer_viewport = true;
	ctx->Extensions.ARB_gpu_shader5 = true;
	ctx->Extensions.ARB_gpu_shader_fp64 = true;
	ctx->Extensions.ARB_gpu_shader_int64 = true;
	ctx->Extensions.ARB_sample_shading = true;
	ctx->Extensions.ARB_shader_bit_encoding = true;
	ctx->Extensions.ARB_shader_draw_parameters = true;
	ctx->Extensions.ARB_shader_stencil_export = true;
	ctx->Extensions.ARB_shader_texture_lod = true;
	ctx->Extensions.ARB_shading_language_420pack = true;
	ctx->Extensions.ARB_shading_language_packing = true;
	ctx->Extensions.ARB_tessellation_shader = true;
	ctx->Extensions.ARB_texture_cube_map_array = true;
	ctx->Extensions.ARB_texture_gather = true;
	ctx->Extensions.ARB_texture_multisample = true;
	ctx->Extensions.ARB_texture_query_levels = true;
	ctx->Extensions.ARB_texture_query_lod = true;
	ctx->Extensions.ARB_uniform_buffer_object = true;
	ctx->Extensions.ARB_viewport_array = true;
	ctx->Extensions.ARB_cull_distance = true;
	ctx->Extensions.ARB_bindless_texture = true;

	ctx->Extensions.OES_EGL_image_external = true;
	ctx->Extensions.OES_standard_derivatives = true;
	ctx->Extensions.OES_texture_3D = true;

	ctx->Extensions.EXT_gpu_shader4 = true;
	ctx->Extensions.EXT_shader_integer_mix = true;
	ctx->Extensions.EXT_texture_array = true;

	ctx->Extensions.MESA_shader_integer_functions = true;

	ctx->Extensions.NV_texture_rectangle = true;

	/* 1.20 minimums. */
	ctx->Const.MaxLights = 8;
	ctx->Const.MaxClipPlanes = 6;
	ctx->Const.MaxTextureUnits = 2;
	ctx->Const.MaxTextureCoordUnits = 2;
	ctx->Const.Program[MESA_SHADER_VERTEX].MaxAttribs = 16;

	ctx->Const.MaxCombinedTextureImageUnits = 2;

	ctx->Const.MaxDrawBuffers = 1;
	ctx->Const.MaxComputeWorkGroupCount[0] = 65535;
	ctx->Const.MaxComputeWorkGroupCount[1] = 65535;
	ctx->Const.MaxComputeWorkGroupCount[2] = 65535;
	ctx->Const.MaxComputeWorkGroupSize[0] = 1024;
	ctx->Const.MaxComputeWorkGroupSize[1] = 1024;
	ctx->Const.MaxComputeWorkGroupSize[2] = 64;
	ctx->Const.MaxComputeWorkGroupInvocations = 1024;
	ctx->Const.MaxComputeVariableGroupSize[0] = 512;
	ctx->Const.MaxComputeVariableGroupSize[1] = 512;
	ctx->Const.MaxComputeVariableGroupSize[2] = 64;
	ctx->Const.MaxComputeVariableGroupInvocations = 512;

    /* Set up default shader compiler options. */
	struct gl_shader_compiler_options options;
	memset(&options, 0, sizeof(options));
	options.MaxIfDepth = UINT_MAX;

	for (int sh = 0; sh < MESA_SHADER_STAGES; ++sh)
		memcpy(&ctx->Const.ShaderCompilerOptions[sh], &options, sizeof(options));

	_mesa_locale_init();

	return ctx;
}

void CafeGLSLCompiler::_InitGLContext()
{
	glCtx = _GLSLCreateDefaultContext(API_OPENGL_COMPAT, 450);
	gl_context *ctx = glCtx;

    const int CAFE_MAX_UNIFORM_BLOCKS_PER_STAGE = 16;
    const int CAFE_MAX_UNIFORM_COMPONENTS_PER_BLOCK = 4096 * 4; // 4096 vec4, 64KiB. The actual HW limit might be higher (testing required)
    const int CAFE_MAX_UNIFORM_COMPONENTS_COMBINED = CAFE_MAX_UNIFORM_BLOCKS_PER_STAGE * CAFE_MAX_UNIFORM_COMPONENTS_PER_BLOCK;
    const int CAFE_MAX_TEXTURE_UNITS_PER_STAGE = 18;
    const int CAFE_MAX_VERTEX_STREAMS = 32;

    // taken from the standalone compiler
	ctx->Extensions.ARB_ES3_compatibility = true;
	ctx->Extensions.ARB_ES3_1_compatibility = true;
	ctx->Extensions.ARB_ES3_2_compatibility = true;

    ctx->Const.MaxComputeWorkGroupCount[0] = 65535;
	ctx->Const.MaxComputeWorkGroupCount[1] = 65535;
	ctx->Const.MaxComputeWorkGroupCount[2] = 65535;
	ctx->Const.MaxComputeWorkGroupSize[0] = 1024;
	ctx->Const.MaxComputeWorkGroupSize[1] = 1024;
	ctx->Const.MaxComputeWorkGroupSize[2] = 64;
	ctx->Const.MaxComputeWorkGroupInvocations = 1024;
	ctx->Const.MaxComputeSharedMemorySize = 32768;
	ctx->Const.MaxComputeVariableGroupSize[0] = 512;
	ctx->Const.MaxComputeVariableGroupSize[1] = 512;
	ctx->Const.MaxComputeVariableGroupSize[2] = 64;
	ctx->Const.MaxComputeVariableGroupInvocations = 512;
	ctx->Const.Program[MESA_SHADER_COMPUTE].MaxTextureImageUnits = 16;
	ctx->Const.Program[MESA_SHADER_COMPUTE].MaxCombinedUniformComponents = 1024;
	ctx->Const.Program[MESA_SHADER_COMPUTE].MaxInputComponents = 0;	 /* not used */
	ctx->Const.Program[MESA_SHADER_COMPUTE].MaxOutputComponents = 0; /* not used */
	ctx->Const.Program[MESA_SHADER_COMPUTE].MaxImageUniforms = 8;

    ctx->Const.MaxUniformBufferBindings = 0; // glUniformBlockBinding limit (API only)
    ctx->Const.MaxClipPlanes = 8;
    ctx->Const.MaxDrawBuffers = 8;
    ctx->Const.MinProgramTexelOffset = -8;
    ctx->Const.MaxProgramTexelOffset = 7;
    ctx->Const.MaxLights = 8;
    ctx->Const.MaxTextureCoordUnits = 8;
    ctx->Const.MaxTextureUnits = 2;

    ctx->Const.MaxVertexStreams = CAFE_MAX_VERTEX_STREAMS;
    ctx->Const.MaxTransformFeedbackBuffers = 4;
    ctx->Const.MaxShaderStorageBufferBindings = 4;
    ctx->Const.MaxShaderStorageBlockSize = 4096;
    ctx->Const.MaxAtomicBufferBindings = 4;

    ctx->Const.Program[MESA_SHADER_VERTEX].MaxAttribs = CAFE_MAX_VERTEX_STREAMS;
    ctx->Const.Program[MESA_SHADER_VERTEX].MaxUniformBlocks = CAFE_MAX_UNIFORM_BLOCKS_PER_STAGE;
    ctx->Const.Program[MESA_SHADER_VERTEX].MaxUniformComponents = CAFE_MAX_UNIFORM_COMPONENTS_PER_BLOCK;
    ctx->Const.Program[MESA_SHADER_VERTEX].MaxCombinedUniformComponents = CAFE_MAX_UNIFORM_COMPONENTS_COMBINED;
    ctx->Const.Program[MESA_SHADER_VERTEX].MaxInputComponents = 0; /* not used */
    ctx->Const.Program[MESA_SHADER_VERTEX].MaxOutputComponents = 128;
    ctx->Const.Program[MESA_SHADER_VERTEX].MaxTextureImageUnits = CAFE_MAX_TEXTURE_UNITS_PER_STAGE;

    ctx->Const.Program[MESA_SHADER_GEOMETRY].MaxUniformBlocks = CAFE_MAX_UNIFORM_BLOCKS_PER_STAGE;
    ctx->Const.Program[MESA_SHADER_GEOMETRY].MaxUniformComponents = CAFE_MAX_UNIFORM_COMPONENTS_PER_BLOCK;
    ctx->Const.Program[MESA_SHADER_GEOMETRY].MaxCombinedUniformComponents = CAFE_MAX_UNIFORM_COMPONENTS_COMBINED;
    ctx->Const.Program[MESA_SHADER_GEOMETRY].MaxInputComponents = ctx->Const.Program[MESA_SHADER_VERTEX].MaxOutputComponents;
    ctx->Const.Program[MESA_SHADER_GEOMETRY].MaxOutputComponents = 128;
    ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxTextureImageUnits = CAFE_MAX_TEXTURE_UNITS_PER_STAGE;

    ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxUniformBlocks = CAFE_MAX_UNIFORM_BLOCKS_PER_STAGE;
    ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxUniformComponents = CAFE_MAX_UNIFORM_COMPONENTS_PER_BLOCK;
    ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxCombinedUniformComponents = CAFE_MAX_UNIFORM_COMPONENTS_COMBINED;
    ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxInputComponents = ctx->Const.Program[MESA_SHADER_GEOMETRY].MaxOutputComponents;
    ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxOutputComponents = 0; /* not used */
    ctx->Const.Program[MESA_SHADER_GEOMETRY].MaxTextureImageUnits = CAFE_MAX_TEXTURE_UNITS_PER_STAGE;

    ctx->Const.Program[MESA_SHADER_COMPUTE].MaxUniformBlocks = CAFE_MAX_UNIFORM_BLOCKS_PER_STAGE;
    ctx->Const.Program[MESA_SHADER_COMPUTE].MaxUniformComponents = CAFE_MAX_UNIFORM_COMPONENTS_PER_BLOCK;
    ctx->Const.Program[MESA_SHADER_COMPUTE].MaxCombinedUniformComponents = CAFE_MAX_UNIFORM_COMPONENTS_COMBINED;
    ctx->Const.Program[MESA_SHADER_COMPUTE].MaxInputComponents = 0; /* not used */
    ctx->Const.Program[MESA_SHADER_COMPUTE].MaxOutputComponents = 0; /* not used */
    ctx->Const.Program[MESA_SHADER_COMPUTE].MaxTextureImageUnits = CAFE_MAX_TEXTURE_UNITS_PER_STAGE;
    ctx->Const.Program[MESA_SHADER_COMPUTE].MaxAtomicBuffers = 8; // todo - does Latte support atomic buffers?
    ctx->Const.Program[MESA_SHADER_COMPUTE].MaxAtomicCounters = 8;

    // note: MaxUniformComponents is the total for non-block uniforms (which we currently reassign to uniform buffer 15)

    // uniform block max size
    ctx->Const.MaxUniformBlockSize = CAFE_MAX_UNIFORM_COMPONENTS_PER_BLOCK * sizeof(uint32_t);

    // combined limits
    ctx->Const.MaxCombinedTextureImageUnits =
        ctx->Const.Program[MESA_SHADER_VERTEX].MaxTextureImageUnits +
        ctx->Const.Program[MESA_SHADER_GEOMETRY].MaxTextureImageUnits +
        ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxTextureImageUnits;

    ctx->Const.MaxCombinedUniformBlocks = ctx->Const.MaxUniformBufferBindings =
            ctx->Const.Program[MESA_SHADER_VERTEX].MaxUniformBlocks +
            ctx->Const.Program[MESA_SHADER_GEOMETRY].MaxUniformBlocks +
            ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxUniformBlocks;

    ctx->Const.MaxGeometryOutputVertices = 256;
    ctx->Const.MaxGeometryTotalOutputComponents = 1024;

    ctx->Const.MaxVarying = 60 / 4;

	ctx->Const.GenerateTemporaryNames = true;
	ctx->Const.MaxPatchVertices = 32;

	/* GL_ARB_explicit_uniform_location, GL_MAX_UNIFORM_LOCATIONS */
	ctx->Const.MaxUserAssignableUniformLocations = 4 * MESA_SHADER_STAGES * MAX_UNIFORMS;

    ctx->Const.NativeIntegers = 1;
    // other stuff from _mesa_init_constants missing?
}

static void winsysDummy_radeon_query_info(struct radeon_winsys *rws,
									 struct radeon_info *info,
									 bool enable_smart_access_memory,
									 bool disable_smart_access_memory)
{
	memset(info, 0, sizeof(struct radeon_info));
	info->family = LATTE_FAMILY_CHIP;
	info->gfx_level = LATTE_GFX_LEVEL;
}

static struct radeon_winsys_ctx *winsysDummy_radeon_drm_ctx_create(struct radeon_winsys *ws,
															  enum radeon_ctx_priority priority)
{
	return (struct radeon_winsys_ctx *)(uintptr_t)1; // must not return zero or Mesa will assume failure
}

void winsysDummy_radeon_drm_ctx_destroy(struct radeon_winsys_ctx *ctx)
{

}

static bool winsysDummy_radeon_drm_cs_create(struct radeon_cmdbuf *rcs,
							struct radeon_winsys_ctx *ctx,
							enum amd_ip_type ip_type,
							void (*flush)(void *ctx, unsigned flags,
										  struct pipe_fence_handle **fence),
							void *flush_ctx,
							bool allow_context_lost)
{
	return true;
}

bool winsysDummy_radeon_unref(struct radeon_winsys *ws)
{
	// this normally decrements the ref counter and returns true if the object should be destroyed
	// but our winsys object is persistent and only used once so it we always assume that new ref count is zero
	return true;
}

void winsysDummy_radeon_destroy(struct radeon_winsys *ws)
{
	// see comment in winsysDummy_radeon_unref
	// this normally immediately destroys the winsys obj
}

void winsysDummy_radeon_cs_destroy(struct radeon_cmdbuf *cs)
{

}

void winsysDummy_radeon_fence_reference(struct pipe_fence_handle **dst, struct pipe_fence_handle *src)
{

}

radeon_winsys winsys{};

void CafeGLSLCompiler::_InitScreen()
{
	pipe_screen_config screenConfig{};

	winsys.query_info = winsysDummy_radeon_query_info;
	winsys.ctx_create = winsysDummy_radeon_drm_ctx_create;
	winsys.ctx_destroy = winsysDummy_radeon_drm_ctx_destroy;
	winsys.cs_create = winsysDummy_radeon_drm_cs_create;
	winsys.cs_destroy = winsysDummy_radeon_cs_destroy;
	winsys.unref = winsysDummy_radeon_unref;
	winsys.destroy = winsysDummy_radeon_destroy;
	winsys.fence_reference = winsysDummy_radeon_fence_reference;

	r600Screen = (r600_screen *)r600_screen_create(&winsys, &screenConfig);
	r600Screen->b.debug_flags = DBG_NIR_SB; // apply extra optimizations
}

void _InitSTContext(struct st_context *st, struct gl_context* ctx, struct pipe_screen *screen)
{
    // mostly borrowed from st_create_context_priv
    ctx->Const.PackedDriverUniformStorage =
            screen->get_param(screen, PIPE_CAP_PACKED_UNIFORMS);

    ctx->Const.BitmapUsesRed =
            screen->is_format_supported(screen, PIPE_FORMAT_R8_UNORM,
                                        PIPE_TEXTURE_2D, 0, 0,
                                        PIPE_BIND_SAMPLER_VIEW);

    ctx->Const.QueryCounterBits.Timestamp =
            screen->get_param(screen, PIPE_CAP_QUERY_TIMESTAMP_BITS);

    st->has_stencil_export =
            screen->get_param(screen, PIPE_CAP_SHADER_STENCIL_EXPORT);
    st->has_etc1 = screen->is_format_supported(screen, PIPE_FORMAT_ETC1_RGB8,
                                               PIPE_TEXTURE_2D, 0, 0,
                                               PIPE_BIND_SAMPLER_VIEW);
    st->has_etc2 = screen->is_format_supported(screen, PIPE_FORMAT_ETC2_RGB8,
                                               PIPE_TEXTURE_2D, 0, 0,
                                               PIPE_BIND_SAMPLER_VIEW);

    st->has_astc_2d_ldr =
            screen->is_format_supported(screen, PIPE_FORMAT_ASTC_4x4_SRGB,
                                        PIPE_TEXTURE_2D, 0, 0, PIPE_BIND_SAMPLER_VIEW);
    st->has_astc_5x5_ldr =
            screen->is_format_supported(screen, PIPE_FORMAT_ASTC_5x5_SRGB,
                                        PIPE_TEXTURE_2D, 0, 0, PIPE_BIND_SAMPLER_VIEW);
    st->has_s3tc = screen->is_format_supported(screen, PIPE_FORMAT_DXT5_RGBA,
                                               PIPE_TEXTURE_2D, 0, 0,
                                               PIPE_BIND_SAMPLER_VIEW);
    st->has_rgtc = screen->is_format_supported(screen, PIPE_FORMAT_RGTC2_UNORM,
                                               PIPE_TEXTURE_2D, 0, 0,
                                               PIPE_BIND_SAMPLER_VIEW);
    st->has_latc = screen->is_format_supported(screen, PIPE_FORMAT_LATC2_UNORM,
                                               PIPE_TEXTURE_2D, 0, 0,
                                               PIPE_BIND_SAMPLER_VIEW);
    st->has_bptc = screen->is_format_supported(screen, PIPE_FORMAT_BPTC_SRGBA,
                                               PIPE_TEXTURE_2D, 0, 0,
                                               PIPE_BIND_SAMPLER_VIEW);
    st->force_persample_in_shader =
            screen->get_param(screen, PIPE_CAP_SAMPLE_SHADING) &&
            !screen->get_param(screen, PIPE_CAP_FORCE_PERSAMPLE_INTERP);
    st->has_shareable_shaders = screen->get_param(screen,
                                                  PIPE_CAP_SHAREABLE_SHADERS);
    st->needs_texcoord_semantic =
            screen->get_param(screen, PIPE_CAP_TGSI_TEXCOORD);
    st->apply_texture_swizzle_to_border_color =
            !!(screen->get_param(screen, PIPE_CAP_TEXTURE_BORDER_COLOR_QUIRK) &
               (PIPE_QUIRK_TEXTURE_BORDER_COLOR_SWIZZLE_NV50 |
                PIPE_QUIRK_TEXTURE_BORDER_COLOR_SWIZZLE_R600));
    st->use_format_with_border_color =
            !!(screen->get_param(screen, PIPE_CAP_TEXTURE_BORDER_COLOR_QUIRK) &
               PIPE_QUIRK_TEXTURE_BORDER_COLOR_SWIZZLE_FREEDRENO);
    st->alpha_border_color_is_not_w =
            !!(screen->get_param(screen, PIPE_CAP_TEXTURE_BORDER_COLOR_QUIRK) &
               PIPE_QUIRK_TEXTURE_BORDER_COLOR_SWIZZLE_ALPHA_NOT_W);
    st->emulate_gl_clamp =
            !screen->get_param(screen, PIPE_CAP_GL_CLAMP);
    st->texture_buffer_sampler =
            screen->get_param(screen, PIPE_CAP_TEXTURE_BUFFER_SAMPLER);
    st->has_time_elapsed =
            screen->get_param(screen, PIPE_CAP_QUERY_TIME_ELAPSED);
    st->has_half_float_packing =
            screen->get_param(screen, PIPE_CAP_SHADER_PACK_HALF_FLOAT);
    st->has_multi_draw_indirect =
            screen->get_param(screen, PIPE_CAP_MULTI_DRAW_INDIRECT);
    st->has_indirect_partial_stride =
            screen->get_param(screen, PIPE_CAP_MULTI_DRAW_INDIRECT_PARTIAL_STRIDE);
    st->has_occlusion_query =
            screen->get_param(screen, PIPE_CAP_OCCLUSION_QUERY);
    st->has_single_pipe_stat =
            screen->get_param(screen, PIPE_CAP_QUERY_PIPELINE_STATISTICS_SINGLE);
    st->has_pipeline_stat =
            screen->get_param(screen, PIPE_CAP_QUERY_PIPELINE_STATISTICS);
    st->has_indep_blend_func =
            screen->get_param(screen, PIPE_CAP_INDEP_BLEND_FUNC);
    st->needs_rgb_dst_alpha_override =
            screen->get_param(screen, PIPE_CAP_RGB_OVERRIDE_DST_ALPHA_BLEND);
    st->can_dither =
            screen->get_param(screen, PIPE_CAP_DITHERING);
    st->lower_flatshade =
            !screen->get_param(screen, PIPE_CAP_FLATSHADE);
    st->lower_alpha_test =
            !screen->get_param(screen, PIPE_CAP_ALPHA_TEST);
    st->lower_point_size =
            !screen->get_param(screen, PIPE_CAP_POINT_SIZE_FIXED);
    st->lower_two_sided_color =
            !screen->get_param(screen, PIPE_CAP_TWO_SIDED_COLOR);
    st->lower_ucp =
            !screen->get_param(screen, PIPE_CAP_CLIP_PLANES);
    st->prefer_real_buffer_in_constbuf0 =
            screen->get_param(screen, PIPE_CAP_PREFER_REAL_BUFFER_IN_CONSTBUF0);
    st->has_conditional_render =
            screen->get_param(screen, PIPE_CAP_CONDITIONAL_RENDER);
    st->lower_rect_tex =
            !screen->get_param(screen, PIPE_CAP_TEXRECT);
    st->allow_st_finalize_nir_twice = screen->finalize_nir != NULL;

    st->has_hw_atomics =
            screen->get_shader_param(screen, PIPE_SHADER_FRAGMENT,
                                     PIPE_SHADER_CAP_MAX_HW_ATOMIC_COUNTERS)
            ? true : false;

    /* GL limits and extensions */
    //st_init_limits(screen, &ctx->Const, &ctx->Extensions);
    //st_init_extensions(screen, &ctx->Const,
    //                   &ctx->Extensions, &st->options, ctx->API);

    //if (st_have_perfmon(st)) {
    //    ctx->Extensions.AMD_performance_monitor = GL_TRUE;
    //}

    /* Enable shader-based fallbacks for ARB_color_buffer_float if needed. */
    if (screen->get_param(screen, PIPE_CAP_VERTEX_COLOR_UNCLAMPED)) {
        if (!screen->get_param(screen, PIPE_CAP_VERTEX_COLOR_CLAMPED)) {
            st->clamp_vert_color_in_shader = GL_TRUE;
        }

        if (!screen->get_param(screen, PIPE_CAP_FRAGMENT_COLOR_CLAMPED)) {
            st->clamp_frag_color_in_shader = GL_TRUE;
        }

        /* For drivers which cannot do color clamping, it's better to just
         * disable ARB_color_buffer_float in the core profile, because
         * the clamping is deprecated there anyway. */
        if (ctx->API == API_OPENGL_CORE &&
            (st->clamp_frag_color_in_shader || st->clamp_vert_color_in_shader)) {
            st->clamp_vert_color_in_shader = GL_FALSE;
            st->clamp_frag_color_in_shader = GL_FALSE;
            ctx->Extensions.ARB_color_buffer_float = GL_FALSE;
        }
    }

    ctx->Point.MaxSize = MAX2(ctx->Const.MaxPointSize,
                              ctx->Const.MaxPointSizeAA);

    ctx->Const.NoClippingOnCopyTex = screen->get_param(screen,
                                                       PIPE_CAP_NO_CLIP_ON_COPY_TEX);

    ctx->Const.ForceFloat32TexNearest =
            !screen->get_param(screen, PIPE_CAP_TEXTURE_FLOAT_LINEAR);

    //ctx->Const.ShaderCompilerOptions[MESA_SHADER_VERTEX].PositionAlwaysInvariant = options->vs_position_always_invariant;

    //ctx->Const.ShaderCompilerOptions[MESA_SHADER_TESS_EVAL].PositionAlwaysPrecise = options->vs_position_always_precise;

    /* NIR drivers that support tess shaders and compact arrays need to use
     * GLSLTessLevelsAsInputs / PIPE_CAP_GLSL_TESS_LEVELS_AS_INPUTS. The NIR
     * linker doesn't support linking these as compat arrays of sysvals.
     */
    assert(ctx->Const.GLSLTessLevelsAsInputs ||
           !screen->get_param(screen, PIPE_CAP_NIR_COMPACT_ARRAYS) ||
           !ctx->Extensions.ARB_tessellation_shader);

    /* Set which shader types can be compiled at link time. */
    st->shader_has_one_variant[MESA_SHADER_VERTEX] =
            st->has_shareable_shaders &&
            !st->clamp_vert_color_in_shader &&
            !st->lower_point_size &&
            !st->lower_ucp;

    st->shader_has_one_variant[MESA_SHADER_FRAGMENT] =
            st->has_shareable_shaders &&
            !st->lower_flatshade &&
            !st->lower_alpha_test &&
            !st->clamp_frag_color_in_shader &&
            !st->force_persample_in_shader &&
            !st->lower_two_sided_color;

    st->shader_has_one_variant[MESA_SHADER_TESS_CTRL] = st->has_shareable_shaders;
    st->shader_has_one_variant[MESA_SHADER_TESS_EVAL] =
            st->has_shareable_shaders &&
            !st->clamp_vert_color_in_shader &&
            !st->lower_point_size &&
            !st->lower_ucp;

    st->shader_has_one_variant[MESA_SHADER_GEOMETRY] =
            st->has_shareable_shaders &&
            !st->clamp_vert_color_in_shader &&
            !st->lower_point_size &&
            !st->lower_ucp;
    st->shader_has_one_variant[MESA_SHADER_COMPUTE] = st->has_shareable_shaders;

   //if (util_get_cpu_caps()->num_L3_caches == 1 ||
   //     !st->pipe->set_context_param)
   //     st->pin_thread_counter = ST_L3_PINNING_DISABLED;

    st->bitmap.cache.empty = true;

    /*
    if (ctx->Const.ForceGLNamesReuse && ctx->Shared->RefCount == 1) {
        _mesa_HashEnableNameReuse(ctx->Shared->TexObjects);
        _mesa_HashEnableNameReuse(ctx->Shared->ShaderObjects);
        _mesa_HashEnableNameReuse(ctx->Shared->BufferObjects);
        _mesa_HashEnableNameReuse(ctx->Shared->SamplerObjects);
        _mesa_HashEnableNameReuse(ctx->Shared->FrameBuffers);
        _mesa_HashEnableNameReuse(ctx->Shared->RenderBuffers);
        _mesa_HashEnableNameReuse(ctx->Shared->MemoryObjects);
        _mesa_HashEnableNameReuse(ctx->Shared->SemaphoreObjects);
    }*/
    /* SPECviewperf13/sw-04 crashes since a56849ddda6 if Mesa is build with
     * -O3 on gcc 7.5, which doesn't happen with ForceGLNamesReuse, which is
     * the default setting for SPECviewperf because it simulates glGen behavior
     * of closed source drivers.
     */
    /*
    if (ctx->Const.ForceGLNamesReuse)
        _mesa_HashEnableNameReuse(ctx->Query.QueryObjects);*/

    //_mesa_override_extensions(ctx);
    //_mesa_compute_version(ctx);

    ctx->Const.DriverSupportedPrimMask = screen->get_param(screen, PIPE_CAP_SUPPORTED_PRIM_MODES) | BITFIELD_BIT(PIPE_PRIM_PATCHES);


    printf("ctx->Const.PackedDriverUniformStorage: %d\n", ctx->Const.PackedDriverUniformStorage);
    printf("ctx->Const.NativeIntegers: %d\n", ctx->Const.NativeIntegers);

}

void CafeGLSLCompiler::_InitStateContext()
{
	stContext = (struct st_context *)malloc(sizeof(struct st_context));
	memset(stContext, 0, sizeof(struct st_context));

	glCtx->st = stContext;
	stContext->ctx = glCtx;
	stContext->screen = &r600Screen->b.b;

	r600Ctx = (r600_context *)malloc(sizeof(r600_context));
	memset(r600Ctx, 0, sizeof(r600_context));

	r600_init_common_state_functions(r600Ctx);
	stContext->pipe = &r600Ctx->b.b;
	r600Ctx->b.b.screen = &r600Screen->b.b;
	r600Ctx->screen = r600Screen;

	r600Ctx->b.gfx_level = LATTE_GFX_LEVEL;
	r600Ctx->b.family = LATTE_FAMILY_CHIP;

    // ST context
    _InitSTContext(stContext, glCtx, &r600Screen->b.b);

	// isa
	r600Isa = (r600_isa *)malloc(sizeof(r600_isa));
	memset(r600Isa, 0, sizeof(r600_isa));
	r600Ctx->isa = r600Isa;
	r600_isa_init(r600Ctx, r600Ctx->isa);
}

void CafeGLSLCompiler::CleanupCurrentProgram()
{
	if (!shProg)
		return;
	// free IR
	for (unsigned i = 0; i < MESA_SHADER_STAGES; i++)
	{
		if(!shProg->_LinkedShaders[i])
			continue;
		if(!shProg->_LinkedShaders[i]->ir)
			continue;
		ralloc_free(shProg->_LinkedShaders[i]->ir);
		assert(shProg->_LinkedShaders[i]->symbols == nullptr);
		shProg->_LinkedShaders[i]->ir = nullptr;
	}
	// should be calling _mesa_delete_linked_shader ?
	assert(shProg->Type == GL_SHADER_PROGRAM_MESA);
	_mesa_delete_shader_program(glCtx, shProg);
	shProg = nullptr;
}

void CafeGLSLCompiler::GetVertexShaderRegs(VSRegs& vsRegs)
{
	assert(shProg);
	r600_pipe_shader *pipeShader = GetCurrentPipeShader();
	r600_shader* rshader = &pipeShader->shader;
	/* sq_pgm_resources_vs */
	vsRegs.sq_pgm_resources_vs = S_028868_NUM_GPRS(rshader->bc.ngpr) |
			       /*S_028868_DX10_CLAMP(1) |*/ // S_028868_DX10_CLAMP is set by Mesa, but not by GX2. Investigate exact purpose
			       S_028868_STACK_SIZE(rshader->bc.nstack);
	/* vgt_primitiveid_en */
	vsRegs.vgt_primitiveid_en = 0; // related to geometry shaders?
	/* spi_vs_out_config and spi_vs_out_id */
	for(int i=0; i<10; i++)
		vsRegs.spi_vs_out_id[i] = 0;
	uint32_t nparams = 0;
	for (unsigned int i = 0; i < rshader->noutput; i++)
	{
		if (rshader->output[i].spi_sid)
		{
			uint32_t tmp = (0x80 | rshader->output[i].spi_sid) << ((nparams & 3) * 8);
			vsRegs.spi_vs_out_id[nparams / 4] |= tmp;
			nparams++;
		}
	}
	for(unsigned int i=nparams; i<10*4; i++)
	{
		uint32_t tmp = 0xFF << ((i & 3) * 8);
		vsRegs.spi_vs_out_id[i / 4] |= tmp;
	}
	if(nparams < 1)
		nparams = 1;
	vsRegs.spi_vs_out_config = S_0286C4_VS_EXPORT_COUNT(nparams - 1);
	/* num_spi_vs_out_id (not a real register) */
	vsRegs.num_spi_vs_out_id = nparams;
	/* pa_cl_vs_out_cntl */
	vsRegs.pa_cl_vs_out_cntl = S_02881C_VS_OUT_CCDIST0_VEC_ENA((rshader->clip_dist_write & 0x0F) != 0) |
		S_02881C_VS_OUT_CCDIST1_VEC_ENA((rshader->clip_dist_write & 0xF0) != 0) |
		S_02881C_VS_OUT_MISC_VEC_ENA(rshader->vs_out_misc_write) |
		S_02881C_USE_VTX_POINT_SIZE(rshader->vs_out_point_size) |
		S_02881C_USE_VTX_EDGE_FLAG(rshader->vs_out_edgeflag) |
		S_02881C_USE_VTX_RENDER_TARGET_INDX(rshader->vs_out_layer) |
		S_02881C_USE_VTX_VIEWPORT_INDX(rshader->vs_out_viewport);
	/* sq_vtx_semantic_clear */
	vsRegs.sq_vtx_semantic_clear = 0xFFFFFFFF;
	/* num_sq_vtx_semantic and sq_vtx_semantic */
	for (int i = 0; i < 32; i++)
		vsRegs.sq_vtx_semantic[i] = 0xFF;
	for (unsigned int i = 0; i < rshader->ninput; i++)
	{
		auto& inputEntry = rshader->input[i];
		assert(inputEntry.gpr >= 1 && (inputEntry.gpr-1) < 32);
		// GPR 0 is skipped, so GPR indices start at 1
		vsRegs.sq_vtx_semantic[inputEntry.gpr-1] = inputEntry.name - 15; // patching the bias inside mesa code seems very convoluted. So let's fix it here

		/*
		ctx->shader->input[i].name = d->Semantic.Name;
		ctx->shader->input[i].sid = d->Semantic.Index + j;
		ctx->shader->input[i].interpolate = d->Interp.Interpolate;
		ctx->shader->input[i].interpolate_location = d->Interp.Location;
		ctx->shader->input[i].interpolate_location = d->Interp.Location;
		ctx->shader->input[i].gpr = ctx->file_offset[TGSI_FILE_INPUT] + d->Range.First + j;
		*/
	}
	vsRegs.num_sq_vtx_semantic = rshader->ninput;
	/* vgt_strmout_buffer_en */
	vsRegs.vgt_strmout_buffer_en = pipeShader->enabled_stream_buffers_mask;
	/* vgt_vertex_reuse_block_cntl and vgt_hos_reuse_depth */
	vsRegs.vgt_vertex_reuse_block_cntl = 14;
	vsRegs.vgt_hos_reuse_depth = 16;
	if ( nparams > 21 )
    {
      vsRegs.vgt_vertex_reuse_block_cntl = 2;
      vsRegs.vgt_hos_reuse_depth = 4;
    }
}

void CafeGLSLCompiler::GetPixelShaderRegs(PSRegs& psRegs)
{
	assert(shProg);
	r600_pipe_shader *pipeShader = GetCurrentPipeShader();
	r600_shader* rshader = &pipeShader->shader;
	/* preprocess */
	int pos_index = -1;
	int face_index = -1;
	int fixed_pt_position_index = -1;
	uint32_t need_linear = 0;
	for (unsigned int i = 0; i < rshader->ninput; i++)
	{
		if (rshader->input[i].name == TGSI_SEMANTIC_POSITION)
			pos_index = i;
		if (rshader->input[i].interpolate == TGSI_INTERPOLATE_LINEAR)
			need_linear = 1;

		if (rshader->input[i].name == TGSI_SEMANTIC_FACE && face_index == -1)
			face_index = i;
		if (rshader->input[i].name == TGSI_SEMANTIC_SAMPLEID)
			fixed_pt_position_index = i;
	}

    /* sq_pgm_resources_ps */
	psRegs.sq_pgm_resources_ps = S_028850_NUM_GPRS(rshader->bc.ngpr) |
		 	S_028850_DX10_CLAMP(1) |
			 S_028850_STACK_SIZE(rshader->bc.nstack) |
			 S_028850_UNCACHED_FIRST_INST(0);
    /* sq_pgm_exports_ps */
	uint32_t exports_ps = 0;
	for (unsigned int i = 0; i < rshader->noutput; i++)
	{
		if (rshader->output[i].name == TGSI_SEMANTIC_POSITION ||
		    rshader->output[i].name == TGSI_SEMANTIC_STENCIL ||
		    rshader->output[i].name == TGSI_SEMANTIC_SAMPLEMASK)
			exports_ps |= 1;
	}
	uint32_t num_cout = rshader->nr_ps_color_exports;
	exports_ps |= S_028854_EXPORT_COLORS(num_cout);
	if (!exports_ps)
		exports_ps = 2; // mesa clamps this to a minimum of 1, do we need this too?
	psRegs.sq_pgm_exports_ps = exports_ps;
	/* spi_ps_in_control_0 */
	psRegs.spi_ps_in_control_0 = S_0286CC_NUM_INTERP(rshader->ninput) |
				S_0286CC_PERSP_GRADIENT_ENA(1)|
				S_0286CC_LINEAR_GRADIENT_ENA(need_linear);
	if (pos_index != -1)
	{
		psRegs.spi_ps_in_control_0 |= (S_0286CC_POSITION_ENA(1) |
					S_0286CC_POSITION_CENTROID(rshader->input[pos_index].interpolate_location == TGSI_INTERPOLATE_LOC_CENTROID) |
					S_0286CC_POSITION_ADDR(rshader->input[pos_index].gpr) |
					S_0286CC_BARYC_SAMPLE_CNTL(1)) |
					S_0286CC_POSITION_SAMPLE(rshader->input[pos_index].interpolate_location == TGSI_INTERPOLATE_LOC_SAMPLE);
	}
	/* spi_ps_in_control_1 */
	psRegs.spi_ps_in_control_1 = 0;
	if (face_index != -1)
	{
		psRegs.spi_ps_in_control_1 |= S_0286D0_FRONT_FACE_ENA(1) |
			S_0286D0_FRONT_FACE_ADDR(rshader->input[face_index].gpr);
	}
	if (fixed_pt_position_index != -1)
	{
		psRegs.spi_ps_in_control_1 |= S_0286D0_FIXED_PT_POSITION_ENA(1) |
			S_0286D0_FIXED_PT_POSITION_ADDR(rshader->input[fixed_pt_position_index].gpr);
	}
    /* num_spi_ps_input_cntl and spi_ps_input_cntls */
	uint32_t sprite_coord_enable = 0; // todo
	for (unsigned int i = 0; i < 32; i++)
		psRegs.spi_ps_input_cntls[i] = 0;
	for (unsigned int i = 0; i < rshader->ninput; i++)
	{
		uint32_t input_cntl = 0;
		int sid = rshader->input[i].spi_sid;
		sid |= 0x80;
		input_cntl = S_028644_SEMANTIC(sid);

		if (rshader->input[i].name == TGSI_SEMANTIC_COLOR && rshader->input[i].sid == 0)
			input_cntl |= S_028644_DEFAULT_VAL(3);
		if (rshader->input[i].name == TGSI_SEMANTIC_POSITION ||
			rshader->input[i].interpolate == TGSI_INTERPOLATE_CONSTANT)
			input_cntl |= S_028644_FLAT_SHADE(1);
		if (rshader->input[i].name == TGSI_SEMANTIC_PCOORD ||
			(rshader->input[i].name == TGSI_SEMANTIC_TEXCOORD &&
			sprite_coord_enable & (1 << rshader->input[i].sid))) {
			input_cntl |= S_028644_PT_SPRITE_TEX(1);
		}
		if (rshader->input[i].interpolate_location == TGSI_INTERPOLATE_LOC_CENTROID)
			input_cntl |= S_028644_SEL_CENTROID(1);
		if (rshader->input[i].interpolate_location == TGSI_INTERPOLATE_LOC_SAMPLE)
			input_cntl |= S_028644_SEL_SAMPLE(1);
		if (rshader->input[i].interpolate == TGSI_INTERPOLATE_LINEAR)
		{
			need_linear = 1;
			input_cntl |= S_028644_SEL_LINEAR(1);
		}
		psRegs.spi_ps_input_cntls[i] = input_cntl;
	}
	psRegs.num_spi_ps_input_cntl = rshader->ninput;
    /* cb_shader_mask */
	psRegs.cb_shader_mask = rshader->ps_color_export_mask; // unsure
    /* cb_shader_control */
	uint32_t multiwrite = rshader->nr_ps_color_exports > 1 ? 1 : 0;
	psRegs.cb_shader_control = S_028808_MULTIWRITE_ENABLE(multiwrite);
	psRegs.cb_shader_control |= 1; // GX2 seems to set this. In Mesa this is documented as S_028808_FOG_ENABLE
	// more fields todo ?
    /* db_shader_control */
	psRegs.db_shader_control = 0;
	int msaa = 0; // todo
	uint32_t z_export = 0, stencil_export = 0, mask_export = 0;
	for (unsigned int i = 0; i < rshader->noutput; i++)
	{
		if (rshader->output[i].name == TGSI_SEMANTIC_POSITION)
			z_export = 1;
		if (rshader->output[i].name == TGSI_SEMANTIC_STENCIL)
			stencil_export = 1;
		if (rshader->output[i].name == TGSI_SEMANTIC_SAMPLEMASK && msaa)
			mask_export = 1;
	}
	psRegs.db_shader_control |= S_02880C_Z_ORDER(V_02880C_EARLY_Z_THEN_LATE_Z);
	psRegs.db_shader_control |= S_02880C_Z_EXPORT_ENABLE(z_export);
	psRegs.db_shader_control |= S_02880C_STENCIL_REF_EXPORT_ENABLE(stencil_export);
	psRegs.db_shader_control |= S_02880C_MASK_EXPORT_ENABLE(mask_export);
	if (rshader->uses_kill)
		psRegs.db_shader_control |= S_02880C_KILL_ENABLE(1);
    /* spi_input_z */
	psRegs.spi_input_z = 0;
	if (pos_index != -1)
		psRegs.spi_input_z |= S_0286D8_PROVIDE_Z_TO_SPI(1);
}

struct CafeShaderIOInfo
{
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
};

/*
enum glsl_base_type {

    GLSL_TYPE_UINT = 0,
    GLSL_TYPE_INT,
    GLSL_TYPE_FLOAT,
    GLSL_TYPE_FLOAT16,
    GLSL_TYPE_DOUBLE,
    GLSL_TYPE_UINT8,
    GLSL_TYPE_INT8,
    GLSL_TYPE_UINT16,
    GLSL_TYPE_INT16,
    GLSL_TYPE_UINT64,
    GLSL_TYPE_INT64,
    GLSL_TYPE_BOOL,
    GLSL_TYPE_SAMPLER,
    GLSL_TYPE_TEXTURE,
    GLSL_TYPE_IMAGE,
    GLSL_TYPE_ATOMIC_UINT,
    GLSL_TYPE_STRUCT,
    GLSL_TYPE_INTERFACE,
    GLSL_TYPE_ARRAY,
    GLSL_TYPE_VOID,
    GLSL_TYPE_SUBROUTINE,
    GLSL_TYPE_FUNCTION,
    GLSL_TYPE_ERROR
};*/

const char* GetMesaGLSLTypeName(glsl_base_type baseType)
{
    switch(baseType)
    {
    case GLSL_TYPE_UINT:
        return "uint";
    case GLSL_TYPE_INT:
        return "int";
    case GLSL_TYPE_FLOAT:
        return "float";
    case GLSL_TYPE_FLOAT16:
        return "float16";
    case GLSL_TYPE_DOUBLE:
        return "double";
    case GLSL_TYPE_UINT8:
        return "uint8_t";
    case GLSL_TYPE_INT8:
        return "int8_t";
    case GLSL_TYPE_UINT16:
        return "uint16_t";
    case GLSL_TYPE_INT16:
        return "int16_t";
    case GLSL_TYPE_UINT64:
        return "uint64_t";
    case GLSL_TYPE_INT64:
        return "int64_t";
    case GLSL_TYPE_BOOL:
        return "bool";
    case GLSL_TYPE_SAMPLER:
        return "sampler";
    case GLSL_TYPE_TEXTURE:
        return "texture";
    case GLSL_TYPE_IMAGE:
        return "image";
    case GLSL_TYPE_ATOMIC_UINT:
        return "atomic_uint";
    case GLSL_TYPE_STRUCT:
        return "struct";
    case GLSL_TYPE_INTERFACE:
        return "interface";
    case GLSL_TYPE_ARRAY:
        return "array";
    case GLSL_TYPE_VOID:
        return "void";
        default:
            break;
    }
    return "UNKNOWN";
}

GX2ShaderVarType GetGX2TypeFromGLSLTypeInfo(glsl_base_type baseType, uint32_t vectorSize, uint32_t matrixSize)
{
    if(matrixSize != 1)
    {
        // matrix types
        assert(baseType == GLSL_TYPE_FLOAT); // only float matrices are supported
        if(vectorSize == 4 && matrixSize == 4)
            return GX2_SHADER_VAR_TYPE_FLOAT4X4;
        if(vectorSize == 3 && matrixSize == 4)
            return GX2_SHADER_VAR_TYPE_FLOAT3X4;
        if(vectorSize == 2 && matrixSize == 4)
            return GX2_SHADER_VAR_TYPE_FLOAT2X4;
        if(vectorSize == 4 && matrixSize == 3)
            return GX2_SHADER_VAR_TYPE_FLOAT4X3;
        if(vectorSize == 3 && matrixSize == 3)
            return GX2_SHADER_VAR_TYPE_FLOAT3X3;
        if(vectorSize == 2 && matrixSize == 3)
            return GX2_SHADER_VAR_TYPE_FLOAT2X3;
        if(vectorSize == 4 && matrixSize == 2)
            return GX2_SHADER_VAR_TYPE_FLOAT4X2;
        if(vectorSize == 3 && matrixSize == 2)
            return GX2_SHADER_VAR_TYPE_FLOAT3X2;
        if(vectorSize == 2 && matrixSize == 2)
            return GX2_SHADER_VAR_TYPE_FLOAT2X2;
        assert(false);
    }
    // scalars and vectors
    switch(baseType)
    {
        case GLSL_TYPE_UINT:
            switch (vectorSize)
            {
                case 1:
                    return GX2_SHADER_VAR_TYPE_UINT;
                case 2:
                    return GX2_SHADER_VAR_TYPE_UINT2;
                case 3:
                    return GX2_SHADER_VAR_TYPE_UINT3;
                case 4:
                    return GX2_SHADER_VAR_TYPE_UINT4;
                default:
                    break;
            }
            break;
        case GLSL_TYPE_INT:
            switch (vectorSize) {
                case 1:
                    return GX2_SHADER_VAR_TYPE_INT;
                case 2:
                    return GX2_SHADER_VAR_TYPE_INT2;
                case 3:
                    return GX2_SHADER_VAR_TYPE_INT3;
                case 4:
                    return GX2_SHADER_VAR_TYPE_INT4;
                default:
                    break;
            }
            break;
        case GLSL_TYPE_FLOAT:
            switch (vectorSize)
            {
                case 1:
                    return GX2_SHADER_VAR_TYPE_FLOAT;
                case 2:
                    return GX2_SHADER_VAR_TYPE_FLOAT2;
                case 3:
                    return GX2_SHADER_VAR_TYPE_FLOAT3;
                case 4:
                    return GX2_SHADER_VAR_TYPE_FLOAT4;
                default:
                    break;
            }
            break;
        default:
            assert(false);
            break;
    }
    assert(false);
    return GX2_SHADER_VAR_TYPE_FLOAT;
}

void CafeGLSLCompiler::GetShaderIOInfo(CafeShaderIOInfo& shaderIOInfo)
{
    memset(&shaderIOInfo, 0, sizeof(CafeShaderIOInfo));

    gl_program* glProg = GetCurrentGLProgram();

    auto trackUniformBlock = [](CafeShaderIOInfo& shaderIOInfo, const char* name, uint32_t offset, uint32_t size)
    {
        shaderIOInfo.uniformBlockCount++;
        shaderIOInfo.uniformBlocks = (GX2UniformBlock*)realloc(shaderIOInfo.uniformBlocks, sizeof(GX2UniformBlock) * shaderIOInfo.uniformBlockCount);
        shaderIOInfo.uniformBlocks[shaderIOInfo.uniformBlockCount - 1].name = strdup(name);
        shaderIOInfo.uniformBlocks[shaderIOInfo.uniformBlockCount - 1].offset = offset;
        shaderIOInfo.uniformBlocks[shaderIOInfo.uniformBlockCount - 1].size = size;
    };

    auto trackUniformVariable = [](CafeShaderIOInfo& shaderIOInfo, const char* name, uint32_t offset, uint32_t count, uint32_t block, GX2ShaderVarType type)
    {
        shaderIOInfo.uniformVarCount++;
        shaderIOInfo.uniformVars = (GX2UniformVar*)realloc(shaderIOInfo.uniformVars, sizeof(GX2UniformVar) * shaderIOInfo.uniformVarCount);
        shaderIOInfo.uniformVars[shaderIOInfo.uniformVarCount - 1].name = strdup(name);
        shaderIOInfo.uniformVars[shaderIOInfo.uniformVarCount - 1].offset = offset;
        shaderIOInfo.uniformVars[shaderIOInfo.uniformVarCount - 1].count = count;
        shaderIOInfo.uniformVars[shaderIOInfo.uniformVarCount - 1].block = block;
        shaderIOInfo.uniformVars[shaderIOInfo.uniformVarCount - 1].type = type;
    };


    auto trackSamplerVar = [](CafeShaderIOInfo& shaderIOInfo, const char* name, uint32_t location, GX2SamplerVarType type)
    {
        shaderIOInfo.samplerVarCount++;
        shaderIOInfo.samplerVars = (GX2SamplerVar*)realloc(shaderIOInfo.samplerVars, sizeof(GX2SamplerVar) * shaderIOInfo.samplerVarCount);
        shaderIOInfo.samplerVars[shaderIOInfo.samplerVarCount - 1].name = strdup(name);
        shaderIOInfo.samplerVars[shaderIOInfo.samplerVarCount - 1].location = location;
        shaderIOInfo.samplerVars[shaderIOInfo.samplerVarCount - 1].type = type;
    };

	auto trackVertexAttribute = [](CafeShaderIOInfo& shaderIOInfo, const char* name, uint32_t location, GX2ShaderVarType type)
	{
		shaderIOInfo.attribVarCount++;
		shaderIOInfo.attribVars = (GX2AttribVar*)realloc(shaderIOInfo.attribVars, sizeof(GX2AttribVar) * shaderIOInfo.attribVarCount);
		auto& var = shaderIOInfo.attribVars[shaderIOInfo.attribVarCount - 1];
		var.name = strdup(name);
		var.location = location;
		var.type = type;
		var.count = 1; // todo - array support
	};

    auto trackLoopVar = [](CafeShaderIOInfo& shaderIOInfo, uint32_t offset, uint32_t value)
    {
        shaderIOInfo.loopVarCount++;
        shaderIOInfo.loopVars = (GX2LoopVar*)realloc(shaderIOInfo.loopVars, sizeof(GX2LoopVar) * shaderIOInfo.loopVarCount);
        shaderIOInfo.loopVars[shaderIOInfo.loopVarCount - 1].offset = offset;
        shaderIOInfo.loopVars[shaderIOInfo.loopVarCount - 1].value = value;
    };

    auto getUniformOffset = [](gl_program* glProg, gl_shader_program* prog, const gl_uniform_storage* uniform) -> int32_t
    {
        if(uniform->num_driver_storage <= 0)
            return -1;

        assert(uniform->num_driver_storage >= 1);
        return (int8_t*)uniform->driver_storage[0].data - (int8_t*)glProg->Parameters->ParameterValues;

        /*for (int i = 0; i < uniform->num_driver_storage; i++)
        {
            struct gl_uniform_driver_storage *const store = &uniform->driver_storage[i];
            printf("storage%d: %p\n", (int)i, store->data);
        }

        for(uint32_t i=0; i<prog->NumUniformRemapTable; i++)
        {
            if(prog->UniformRemapTable[i] == uniform)
            {
                return uniform->storage - prog->data->UniformDataSlots;
                // prog->data->NumUniformDataSlots
            }
                //return (int32_t)i;
        }*/
        return -1;
    };

    // shProg = gl_shader_program*
    // shProg->Program = gl_program*
    // shProg->data = gl_shader_program_data*      -> Has IO info
    // gl_program has: GLubyte SamplerUnits[MAX_SAMPLERS];

    /*
    sh->Program->SamplersUsed = state.shader_samplers_used;
    sh->Program->sh.ShaderStorageBlocksWriteAccess =
    state.shader_storage_blocks_write_access;
    sh->shadow_samplers = state.shader_shadow_samplers;
    sh->Program->info.num_textures = state.num_shader_samplers;
    sh->Program->info.num_images = state.num_shader_images;*/

    // some variables use a simple incremental counter when not assigned explicit locations
    // see _mesa_program_resource_find_index()
    //int32_t nextProgramInputIndex = 0;
    //int32_t nextProgramOutputIndex = 0;

    for(unsigned int i=0; i<shProg->data->NumProgramResourceList; i++)
    {
        struct gl_resource_name rname;
        struct gl_program_resource *res = &shProg->data->ProgramResourceList[i];
        if(res->Type == GL_UNIFORM_BLOCK || res->Type == GL_SHADER_STORAGE_BLOCK)
        {
            const struct gl_uniform_block* block = (struct gl_uniform_block*)res->Data;//RESOURCE_UBO(res);
            if(!_mesa_program_get_resource_name(res, &rname))
                continue;
            trackUniformBlock(shaderIOInfo, rname.string, block->Binding, block->UniformBufferSize);
            // DebugLog("[UniformBlock] res %u: %s UniformBufferSize: 0x%x Binding: %d", i, rname.string, block->UniformBufferSize, block->Binding);
            continue;
        }
        else if(res->Type == GL_UNIFORM || res->Type == GL_BUFFER_VARIABLE)
        {
            const struct gl_uniform_storage* var = (struct gl_uniform_storage*)res->Data;//RESOURCE_UNI(res);

            if(!_mesa_program_get_resource_name(res, &rname))
                continue;

            if(var->type->is_image())
                continue;
            else if(var->type->is_texture())
                continue;
            else if(var->type->is_sampler())
            {
                GX2SamplerVarType gx2SamplerType = GX2_SAMPLER_VAR_TYPE_SAMPLER_2D;
                glsl_sampler_dim samplerType = (glsl_sampler_dim)var->type->sampler_dimensionality;
                if(samplerType == GLSL_SAMPLER_DIM_1D)
                    gx2SamplerType = GX2_SAMPLER_VAR_TYPE_SAMPLER_1D;
                else if(samplerType == GLSL_SAMPLER_DIM_2D)
                    gx2SamplerType = GX2_SAMPLER_VAR_TYPE_SAMPLER_2D;
                else if(samplerType == GLSL_SAMPLER_DIM_3D)
                    gx2SamplerType = GX2_SAMPLER_VAR_TYPE_SAMPLER_3D;
                else if(samplerType == GLSL_SAMPLER_DIM_CUBE)
                    gx2SamplerType = GX2_SAMPLER_VAR_TYPE_SAMPLER_CUBE;
                else if(samplerType == GLSL_SAMPLER_DIM_RECT)
                    gx2SamplerType = GX2_SAMPLER_VAR_TYPE_SAMPLER_2D;
                else
                {
                    DebugLog("Unknown sampler type %d\n", (int)samplerType);
                }

                /*
                int32_t uniformLocation = -1;
                for(int i=0; i<MESA_SHADER_STAGES; i++)
                {
                    // todo - read from correct stage immediately
                    if( var->opaque[i].active )
                        uniformLocation = var->opaque[i].index;
                }*/ // This gives us reordered location from 0 to n

                int32_t uniformLocation = -1;
                if(var->storage)
                    uniformLocation = var->storage->u;

                trackSamplerVar(shaderIOInfo, rname.string, uniformLocation, gx2SamplerType);
                continue;
            }
            else
            {
                int32_t uniformOffset = getUniformOffset(glProg, shProg, var);

                if(uniformOffset >= 0)
                {
                    trackUniformVariable(shaderIOInfo, rname.string, uniformOffset, var->array_elements, -1, GetGX2TypeFromGLSLTypeInfo(var->type->base_type, var->type->vector_elements, var->type->matrix_columns));
                }

                //DebugLog("[Variable UF Type %d] res %u: %s block_index: %d offset: %d remap_loc: %d atomic_bi: %d uniformOffset: %d", (int)res->Type, i, rname.string, var->block_index, var->offset, var->remap_location, var->atomic_buffer_index, uniformOffset);
                //DebugLog("[Variable UF Type %d] vector_elements: %d matrix_columns: %d length: %d", (int)res->Type, (int)var->type->vector_elements, (int)var->type->matrix_columns, (int)var->type->length);
                //DebugLog("[Variable UF Type %d] array_elements: %d is_matrix: %s base_type: %s", (int)res->Type, (int)var->array_elements, var->type->is_matrix()?"yes":"no", GetMesaGLSLTypeName(var->type->base_type));

                // array_elements -> 0 for scalars, non-zero for arrays
                // vector_elements (width), matrix_columns (height). E.g. mat4 is 4x4, vec4 is 4x1, scalar float is 1x1
                // the base type can be only: GLSL_TYPE_UINT, GLSL_TYPE_INT, GLSL_TYPE_FLOAT

                continue;
            }
        }
        else if(res->Type == GL_PROGRAM_INPUT || res->Type == GL_PROGRAM_OUTPUT)
        {
            const struct gl_shader_variable* var = (struct gl_shader_variable*)res->Data;
            if(!_mesa_program_get_resource_name(res, &rname))
                continue;


            if(res->Type == GL_PROGRAM_INPUT)
			{
				/*trackProgramInputEx(shaderIOInfo, rname.string, var->location);*/
				// if vertex shader than add to vertex attributes
				if (lastCompiledShaderType == SHADER_TYPE::VERTEX_SHADER)
					trackVertexAttribute(shaderIOInfo, rname.string, var->location, GetGX2TypeFromGLSLTypeInfo(var->type->base_type, var->type->vector_elements, var->type->matrix_columns));
			}
            /*else if(res->Type == GL_PROGRAM_OUTPUT)
                trackProgramOutputEx(shaderIOInfo, rname.string, var->location);*/

            //DebugLog("[Variable %s] res %u: %s index: %d explicit-location: %s location: %d", res->Type == GL_PROGRAM_INPUT ? "IN" : "OUT", i, rname.string, var->index, var->explicit_location ? "true" : "false", var->location);
            continue;
        }
    }

    // loop constants
    // Mesa seems to use only the first loop register
    // the reg offsets stored in GX2LoopVar are relative to the type's stage loop const base (PS=0, VS=32, GS=64), therefore always use offset zero
    trackLoopVar(shaderIOInfo, 0, 0x01000FFF);
    // ideally we'd set the loop const according to the loop instruction type used by the shader (normal vs dx10)
    // but we don't have that info easily available here. Instead, like Mesa, we set it to a constant that works both for SQ_LOOP_CONST and SQ_LOOP_CONST_DX10

    // for uniform variables (in UBO 15) we can get the offset by:
    // iterating prog->UniformRemapTable/NumUniformRemapTable which is a pointer table for gl_uniform_storage*
    // where each index corresponds to a single 4 byte value (int). A mat4 is 4*4 entries
    // there is also prog->data->UniformDataDefaults for defaults
}

void CafeGLSLCompiler::GetVertexShaderVars(GX2VertexShader* vs)
{
    CafeShaderIOInfo shaderIoInfo;
    GetShaderIOInfo(shaderIoInfo);
    vs->attribVarCount = shaderIoInfo.attribVarCount;
    vs->attribVars = shaderIoInfo.attribVars;
    vs->uniformBlockCount = shaderIoInfo.uniformBlockCount;
    vs->uniformBlocks = shaderIoInfo.uniformBlocks;
    vs->uniformVarCount = shaderIoInfo.uniformVarCount;
    vs->uniformVars = shaderIoInfo.uniformVars;
    vs->samplerVarCount = shaderIoInfo.samplerVarCount;
    vs->samplerVars = shaderIoInfo.samplerVars;
    vs->loopVarCount = shaderIoInfo.loopVarCount;
    vs->loopVars = shaderIoInfo.loopVars;
}

void CafeGLSLCompiler::GetPixelShaderVars(GX2PixelShader* ps)
{
    CafeShaderIOInfo shaderIoInfo;
    GetShaderIOInfo(shaderIoInfo);
    assert(shaderIoInfo.attribVarCount == 0);
    ps->uniformBlockCount = shaderIoInfo.uniformBlockCount;
    ps->uniformBlocks = shaderIoInfo.uniformBlocks;
    ps->uniformVarCount = shaderIoInfo.uniformVarCount;
    ps->uniformVars = shaderIoInfo.uniformVars;
    ps->samplerVarCount = shaderIoInfo.samplerVarCount;
    ps->samplerVars = shaderIoInfo.samplerVars;
    ps->loopVarCount = shaderIoInfo.loopVarCount;
    ps->loopVars = shaderIoInfo.loopVars;
}

/*
void CafeGLSLCompiler::GetPixelShaderVarsDebug(GX2PixelShader* ps)
{
    DebugLog("shProg has %u resources\n", shProg->data->NumProgramResourceList);
    for(unsigned int i=0; i<shProg->data->NumProgramResourceList; i++)
    {
        struct gl_program_resource* res = &shProg->data->ProgramResourceList[i];

        struct gl_resource_name rname;

        if( !_mesa_program_get_resource_name(res, &rname))
            continue;

        if(res->Type == GL_UNIFORM_BLOCK || res->Type == GL_SHADER_STORAGE_BLOCK)
        {
            const struct gl_uniform_block* block = (struct gl_uniform_block*)res->Data;//RESOURCE_UBO(res);

            DebugLog("[UniformBlock] res %u: %s UniformBufferSize: 0x%x Binding: %d", i, rname.string, block->UniformBufferSize, block->Binding);
            continue;
        }
        else if(res->Type == GL_UNIFORM || res->Type == GL_BUFFER_VARIABLE)
        {
            const struct gl_uniform_storage* var = (struct gl_uniform_storage*)res->Data;//RESOURCE_UNI(res);
            // todo ->type field
            const char* typeStr = "ukn";
            if(var->type->is_image())
                typeStr = "image";
            else if(var->type->is_texture())
                typeStr = "texture";
            else if(var->type->is_sampler())
                typeStr = "sampler";
            // type also has:
            // ->sampler_index()
            // ->contains_sampler

            DebugLog("[UniformVariable] res %u: %s block_index: %d offset: %d remap_loc: %d type: %s", i, rname.string, var->block_index, var->offset, var->remap_location, typeStr);
            continue;
        }
        else if(res->Type == GL_PROGRAM_INPUT || res->Type == GL_PROGRAM_OUTPUT)
        {
            const struct gl_shader_variable* var = (struct gl_shader_variable*)res->Data;
            DebugLog("[Variable %s] res %u: %s explicit-location: %s location: %d", res->Type == GL_PROGRAM_INPUT ? "IN" : "OUT", i, rname.string, var->explicit_location ? "true" : "false", var->location);
            continue;
        }

        // GL_PROGRAM_INPUT for glGetActiveAttrib

        DebugLog("res %u: %s", i, rname.string);
    }

    // other helpful functions:
    // unsigned _mesa_program_resource_array_size(struct gl_program_resource *res)
    // static GLint program_resource_location(struct gl_program_resource *res, unsigned array_index)
    // _get_resource_location_index (e.g. for frag output?)

    // attribute info:
    // shProg->AttributeBindings->... ??
    // shProg->FragDataBindings->...
    // but these might be for dynamic binding?

}*/
