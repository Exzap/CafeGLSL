/* CafeGLSL geometry shader hardware test.
 *
 * Draws three panels on the TV and the GamePad, left to right:
 *   A  plain vertex + pixel shader (control: if this is missing, the app is broken)
 *   B  the same triangle through a pass-through geometry shader
 *   C  four points that a geometry shader expands into coloured squares
 *
 * B starts after ~2 seconds and C after ~4, so a GPU hang freezes the screen on the
 * last panel that worked. Every step is appended to sd:/cafeglsl-gs-test.log, which is
 * closed after each line so it survives a hang.
 */
#include <cafeglsl/CafeGLSLCompiler.h>

#include <coreinit/memdefaultheap.h>
#include <vpad/input.h>
#include <gx2/draw.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/shaders.h>
#include <gx2/utils.h>
#include <gx2r/buffer.h>
#include <gx2r/draw.h>
#include <whb/gfx.h>
#include <whb/log.h>
#include <whb/log_cafe.h>
#include <whb/log_udp.h>
#include <whb/proc.h>
#include <whb/sdcard.h>

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

static std::string g_log_path;

static void Log(const char *format, ...)
{
   char line[1024];
   va_list args;
   va_start(args, format);
   vsnprintf(line, sizeof(line), format, args);
   va_end(args);

   WHBLogPrintf("%s", line);
   if (g_log_path.empty())
      return;
   /* Reopened per line so everything written so far is on the card if the GPU hangs. */
   if (FILE *file = fopen(g_log_path.c_str(), "a")) {
      fprintf(file, "%s\n", line);
      fclose(file);
   }
}

static const char kVertexSource[] = R"(
#version 450
layout(location = 0) in vec2 position;
layout(location = 1) in vec4 color;
layout(location = 0) out vec4 vColor;
void main() {
   vColor = color;
   gl_Position = vec4(position, 0.0, 1.0);
}
)";

static const char kPixelSource[] = R"(
#version 450
layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 fragColor;
void main() { fragColor = vColor; }
)";

static const char kPassThroughGeometrySource[] = R"(
#version 450
layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;
layout(location = 0) in vec4 vColor[];
layout(location = 0) out vec4 gColor;
void main() {
   for (int i = 0; i < 3; ++i) {
      gColor = vColor[i];
      gl_Position = gl_in[i].gl_Position;
      EmitVertex();
   }
   EndPrimitive();
}
)";

static const char kExpandGeometrySource[] = R"(
#version 450
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;
layout(location = 0) in vec4 vColor[];
layout(location = 0) out vec4 gColor;
void main() {
   const vec2 halfSize = vec2(0.07, 0.12);
   for (int i = 0; i < 4; ++i) {
      vec2 corner = vec2(float(i & 1), float(i >> 1)) * 2.0 - 1.0;
      gColor = vColor[0];
      gl_Position = gl_in[0].gl_Position + vec4(corner * halfSize, 0.0, 0.0);
      EmitVertex();
   }
   EndPrimitive();
}
)";

struct Vertex {
   float x, y;
   float r, g, b, a;
};

/* Panels sit in thirds of the screen: A left, B middle, C right. */
static const Vertex kTriangleA[] = {
   {-0.95f, -0.6f, 1.0f, 0.0f, 0.0f, 1.0f},
   {-0.40f, -0.6f, 0.0f, 1.0f, 0.0f, 1.0f},
   {-0.675f, 0.6f, 0.0f, 0.0f, 1.0f, 1.0f},
};
static const Vertex kTriangleB[] = {
   {-0.275f, -0.6f, 1.0f, 0.0f, 0.0f, 1.0f},
   {0.275f, -0.6f, 0.0f, 1.0f, 0.0f, 1.0f},
   {0.0f, 0.6f, 0.0f, 0.0f, 1.0f, 1.0f},
};
static const Vertex kPointsC[] = {
   {0.52f, -0.35f, 1.0f, 1.0f, 0.0f, 1.0f},
   {0.82f, -0.35f, 0.0f, 1.0f, 1.0f, 1.0f},
   {0.52f, 0.35f, 1.0f, 0.0f, 1.0f, 1.0f},
   {0.82f, 0.35f, 1.0f, 1.0f, 1.0f, 1.0f},
};

static bool CreateVertexBuffer(GX2RBuffer &buffer, const Vertex *vertices, uint32_t count)
{
   memset(&buffer, 0, sizeof(buffer));
   buffer.flags = static_cast<GX2RResourceFlags>(
      GX2R_RESOURCE_BIND_VERTEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_READ |
      GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ);
   buffer.elemSize = sizeof(Vertex);
   buffer.elemCount = count;
   if (!GX2RCreateBuffer(&buffer))
      return false;
   void *data = GX2RLockBufferEx(&buffer, GX2R_RESOURCE_BIND_NONE);
   memcpy(data, vertices, sizeof(Vertex) * count);
   GX2RUnlockBufferEx(&buffer, GX2R_RESOURCE_BIND_NONE);
   return true;
}

static int AttribLocation(const GX2VertexShader *shader, const char *name, int fallback)
{
   for (uint32_t i = 0; i < shader->attribVarCount; ++i) {
      if (!strcmp(shader->attribVars[i].name, name))
         return static_cast<int>(shader->attribVars[i].location);
   }
   Log("  warning: attribute %s not reflected, using location %d", name, fallback);
   return fallback;
}

static bool CreateFetchShader(GX2FetchShader &fetch, const GX2VertexShader *vertex)
{
   GX2AttribStream attribs[2] = {};
   attribs[0].location = AttribLocation(vertex, "position", 0);
   attribs[0].buffer = 0;
   attribs[0].offset = offsetof(Vertex, x);
   attribs[0].format = GX2_ATTRIB_FORMAT_FLOAT_32_32;
   attribs[0].type = GX2_ATTRIB_INDEX_PER_VERTEX;
   attribs[0].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
   attribs[0].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;
   attribs[1].location = AttribLocation(vertex, "color", 1);
   attribs[1].buffer = 0;
   attribs[1].offset = offsetof(Vertex, r);
   attribs[1].format = GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32;
   attribs[1].type = GX2_ATTRIB_INDEX_PER_VERTEX;
   attribs[1].mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W);
   attribs[1].endianSwap = GX2_ENDIAN_SWAP_DEFAULT;

   const uint32_t size = GX2CalcFetchShaderSizeEx(2, GX2_FETCH_SHADER_TESSELLATION_NONE,
                                                  GX2_TESSELLATION_MODE_DISCRETE);
   auto *program = static_cast<uint8_t *>(
      MEMAllocFromDefaultHeapEx(size, GX2_SHADER_PROGRAM_ALIGNMENT));
   if (!program)
      return false;
   GX2InitFetchShaderEx(&fetch, program, 2, attribs, GX2_FETCH_SHADER_TESSELLATION_NONE,
                        GX2_TESSELLATION_MODE_DISCRETE);
   GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, program, size);
   return true;
}

static void InvalidateProgram(void *program, uint32_t size)
{
   if (program && size)
      GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, program, size);
}

static void LogGeometryShader(const char *name, const GX2VertexShader *vs,
                              const GX2GeometryShader *gs, const GX2PixelShader *ps)
{
   Log("%s: vs size=%u gprs=%u ringItemsize=%u mode=%d", name, vs->size,
       vs->regs.sq_pgm_resources_vs & 0xff, vs->ringItemsize, vs->mode);
   Log("%s: gs size=%u copy size=%u mode=%d ringItemSize=%u", name, gs->size,
       gs->vertexProgramSize, gs->mode, gs->ringItemSize);
   Log("%s: gs regs res_gs=0x%08x out_prim=%u gs_mode=0x%08x res_vs=0x%08x",
       name, gs->regs.sq_pgm_resources_gs, gs->regs.vgt_gs_out_prim_type,
       gs->regs.vgt_gs_mode, gs->regs.sq_pgm_resources_vs);
   Log("%s: gs regs vert_itemsize=%u out_config=0x%08x n_out_id=%u out_id0=0x%08x "
       "pa_cl_vs_out_cntl=0x%08x strmout=0x%x",
       name, gs->regs.sq_gs_vert_itemsize, gs->regs.spi_vs_out_config,
       gs->regs.num_spi_vs_out_id, gs->regs.spi_vs_out_id[0],
       gs->regs.pa_cl_vs_out_cntl, gs->regs.vgt_strmout_buffer_en);
   Log("%s: ps size=%u inputs=%u input_cntl0=0x%08x", name, ps->size,
       ps->regs.num_spi_ps_input_cntl, ps->regs.spi_ps_input_cntls[0]);
}

struct GeometryProgram {
   GX2VertexShader *vs = nullptr;
   GX2GeometryShader *gs = nullptr;
   GX2PixelShader *ps = nullptr;
};

static bool CompileGeometryProgram(const char *name, const char *gs_source,
                                   GeometryProgram &program)
{
   char log[2048] = {};
   const GX2ShaderMode mode = CompileGeometryShaderSet(
      kVertexSource, gs_source, kPixelSource, GLSL_COMPILE_AUTO,
      &program.vs, &program.gs, &program.ps, log, sizeof(log), GLSL_COMPILER_FLAG_NONE);
   if (mode == GLSL_SHADER_MODE_ERROR) {
      Log("%s: compile FAILED: %s", name, log);
      return false;
   }
   Log("%s: compiled, mode=%d%s%s", name, mode, log[0] ? " log: " : "", log);
   LogGeometryShader(name, program.vs, program.gs, program.ps);
   InvalidateProgram(program.vs->program, program.vs->size);
   InvalidateProgram(program.gs->program, program.gs->size);
   InvalidateProgram(program.gs->vertexProgram, program.gs->vertexProgramSize);
   InvalidateProgram(program.ps->program, program.ps->size);
   return true;
}

struct Scene {
   GX2VertexShader *plain_vs = nullptr;
   GX2PixelShader *plain_ps = nullptr;
   GX2FetchShader fetch = {};
   GeometryProgram pass_through;
   GeometryProgram expand;
   bool pass_through_ok = false;
   bool expand_ok = false;
   void *input_ring = nullptr;
   uint32_t input_ring_size = 0;
   void *output_ring = nullptr;
   uint32_t output_ring_size = 0;
   GX2RBuffer triangle_a = {};
   GX2RBuffer triangle_b = {};
   GX2RBuffer points_c = {};
};

static void DrawGeometryProgram(Scene &scene, const GeometryProgram &program,
                                GX2RBuffer &buffer, GX2PrimitiveMode primitive)
{
   GX2SetShaderMode(GX2_SHADER_MODE_GEOMETRY_SHADER);
   GX2SetGeometryShaderInputRingBuffer(scene.input_ring, scene.input_ring_size);
   GX2SetGeometryShaderOutputRingBuffer(scene.output_ring, scene.output_ring_size);
   GX2SetFetchShader(&scene.fetch);
   GX2SetVertexShader(program.vs);
   GX2SetGeometryShader(program.gs);
   GX2SetPixelShader(program.ps);
   GX2RSetAttributeBuffer(&buffer, 0, buffer.elemSize, 0);
   GX2DrawEx(primitive, buffer.elemCount, 0, 1);
}

static void DrawScene(Scene &scene, bool draw_b, bool draw_c)
{
   GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);
   GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS);

   /* A: no geometry shader. */
   GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
   GX2SetFetchShader(&scene.fetch);
   GX2SetVertexShader(scene.plain_vs);
   GX2SetPixelShader(scene.plain_ps);
   GX2RSetAttributeBuffer(&scene.triangle_a, 0, scene.triangle_a.elemSize, 0);
   GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, 3, 0, 1);

   if (draw_b && scene.pass_through_ok)
      DrawGeometryProgram(scene, scene.pass_through, scene.triangle_b,
                          GX2_PRIMITIVE_MODE_TRIANGLES);
   if (draw_c && scene.expand_ok)
      DrawGeometryProgram(scene, scene.expand, scene.points_c, GX2_PRIMITIVE_MODE_POINTS);

   GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
}

static bool SetUp(Scene &scene)
{
   InitGLSLCompiler();
   Log("CafeGLSL %s", GetGLSLCompilerVersion());

   char log[2048] = {};
   const GX2ShaderMode mode = CompileShaderPair(kVertexSource, kPixelSource, GLSL_COMPILE_AUTO,
                                                &scene.plain_vs, &scene.plain_ps,
                                                log, sizeof(log), GLSL_COMPILER_FLAG_NONE);
   if (mode == GLSL_SHADER_MODE_ERROR) {
      Log("A: compile FAILED: %s", log);
      return false;
   }
   Log("A: compiled, mode=%d vs size=%u ps size=%u", mode, scene.plain_vs->size,
       scene.plain_ps->size);
   InvalidateProgram(scene.plain_vs->program, scene.plain_vs->size);
   InvalidateProgram(scene.plain_ps->program, scene.plain_ps->size);

   if (!CreateFetchShader(scene.fetch, scene.plain_vs)) {
      Log("fetch shader allocation FAILED");
      return false;
   }

   scene.pass_through_ok = CompileGeometryProgram("B", kPassThroughGeometrySource,
                                                  scene.pass_through);
   scene.expand_ok = CompileGeometryProgram("C", kExpandGeometrySource, scene.expand);

   /* One pair of rings, sized for the larger of the two geometry programs. */
   uint32_t input_item = 0, output_item = 0;
   for (const GeometryProgram *program : {&scene.pass_through, &scene.expand}) {
      if (!program->gs)
         continue;
      if (program->vs->ringItemsize > input_item)
         input_item = program->vs->ringItemsize;
      if (program->gs->ringItemSize > output_item)
         output_item = program->gs->ringItemSize;
   }
   if (input_item && output_item) {
      scene.input_ring_size = GX2CalcGeometryShaderInputRingBufferSize(input_item);
      scene.output_ring_size = GX2CalcGeometryShaderOutputRingBufferSize(output_item);
      scene.input_ring = MEMAllocFromDefaultHeapEx(scene.input_ring_size, 256);
      scene.output_ring = MEMAllocFromDefaultHeapEx(scene.output_ring_size, 256);
      Log("rings: input %u bytes at %p, output %u bytes at %p", scene.input_ring_size,
          scene.input_ring, scene.output_ring_size, scene.output_ring);
      if (!scene.input_ring || !scene.output_ring) {
         Log("ring allocation FAILED");
         scene.pass_through_ok = scene.expand_ok = false;
      }
   }

   if (!CreateVertexBuffer(scene.triangle_a, kTriangleA, 3) ||
       !CreateVertexBuffer(scene.triangle_b, kTriangleB, 3) ||
       !CreateVertexBuffer(scene.points_c, kPointsC, 4)) {
      Log("vertex buffer creation FAILED");
      return false;
   }
   return true;
}

static void TearDown(Scene &scene)
{
   /* No GX2DrawDone() here: once WHBProcIsRunning() is false WHB has already
    * released the foreground, the GPU is idle and waiting on it never returns.
    */
   Log("teardown: freeing buffers");
   GX2RDestroyBufferEx(&scene.triangle_a, GX2R_RESOURCE_BIND_NONE);
   GX2RDestroyBufferEx(&scene.triangle_b, GX2R_RESOURCE_BIND_NONE);
   GX2RDestroyBufferEx(&scene.points_c, GX2R_RESOURCE_BIND_NONE);
   if (scene.input_ring)
      MEMFreeToDefaultHeap(scene.input_ring);
   if (scene.output_ring)
      MEMFreeToDefaultHeap(scene.output_ring);
   if (scene.fetch.program)
      MEMFreeToDefaultHeap(scene.fetch.program);
   for (GeometryProgram *program : {&scene.pass_through, &scene.expand}) {
      FreeShaders(program->vs, program->ps);
      FreeGeometryShader(program->gs);
   }
   FreeShaders(scene.plain_vs, scene.plain_ps);
   Log("teardown: shaders freed");
   DestroyGLSLCompiler();
   Log("teardown: compiler destroyed");
}

int main(int argc, char **argv)
{
   WHBProcInit();
   WHBLogCafeInit();
   WHBLogUdpInit();
   if (WHBMountSdCard()) {
      g_log_path = std::string(WHBGetSdCardMountPath()) + "/cafeglsl-gs-test.log";
      if (FILE *file = fopen(g_log_path.c_str(), "w"))
         fclose(file);
   }
   WHBGfxInit();
   Log("CafeGLSL geometry shader test started");

   Scene scene;
   const bool ready = SetUp(scene);
   Log(ready ? "setup done: A always, B from frame 120, C from frame 240"
             : "setup FAILED, drawing nothing");

   uint32_t frame = 0;
   while (WHBProcIsRunning()) {
      /* Aroma's button combos (screenshots and the like) only see input the
       * app reads, so poll the GamePad even though the test ignores it.
       */
      VPADStatus vpad;
      VPADReadError vpad_error;
      VPADRead(VPAD_CHAN_0, &vpad, 1, &vpad_error);

      const bool draw_b = frame >= 120;
      const bool draw_c = frame >= 240;
      const bool first_b = frame == 120 && scene.pass_through_ok;
      const bool first_c = frame == 240 && scene.expand_ok;
      if (first_b)
         Log("frame %u: submitting first B draw (pass-through GS)", frame);
      if (first_c)
         Log("frame %u: submitting first C draw (points to squares GS)", frame);

      WHBGfxBeginRender();
      WHBGfxBeginRenderTV();
      WHBGfxClearColor(0.12f, 0.12f, 0.16f, 1.0f);
      if (ready)
         DrawScene(scene, draw_b, draw_c);
      WHBGfxFinishRenderTV();
      WHBGfxBeginRenderDRC();
      WHBGfxClearColor(0.12f, 0.12f, 0.16f, 1.0f);
      if (ready)
         DrawScene(scene, draw_b, draw_c);
      WHBGfxFinishRenderDRC();
      WHBGfxFinishRender();

      if (frame == 0 && ready) {
         GX2DrawDone();
         Log("frame 0: A finished on the GPU");
      }
      if (first_b || first_c) {
         /* Blocks forever if the GPU hung: the log then ends at "submitting". */
         GX2DrawDone();
         Log("frame %u: %s finished on the GPU", frame, first_b ? "B" : "C");
      }
      if (frame == 600)
         Log("frame 600: still running, all enabled panels drawing");
      ++frame;
   }

   Log("exiting after %u frames", frame);
   if (ready)
      TearDown(scene);
   WHBGfxShutdown();
   Log("graphics shut down");
   WHBUnmountSdCard();
   WHBProcShutdown();
   return 0;
}
