#include "../../cafe_gx2.h"

#include <coreinit/cache.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <gx2/draw.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/shaders.h>
#include <gx2/state.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <whb/log.h>
#include <whb/log_cafe.h>
#include <whb/log_console.h>
#include <whb/proc.h>
#include <whb/sdcard.h>

void
GX2SetComputeShader(const GX2ComputeShader *shader);
void
GX2SetShaderExportBuffer(void *buffer, uint32_t size);
void
GX2DispatchCompute(const uint32_t *groups);
void
GX2SetComputeUniformBlock(uint32_t location, uint32_t size, const void *data);

static uint32_t sOutput[16384] __attribute__((aligned(256)));
static uint32_t sInput[16384] __attribute__((aligned(256)));
static uint32_t sGroups[4] __attribute__((aligned(256))) = {2, 3, 2, 0};
static uint32_t sParameters[64] __attribute__((aligned(256)));
static const uint32_t sElementCount = 1537;
static const uint32_t sMultiplier   = 3;
static const uint32_t sOffset       = 7;

static int
LoadShader(const char *path, GX2ComputeShader *shader, uint8_t **metadata)
{
   FILE *file = fopen(path, "rb");
   uint32_t header[8];
   int result = 0;
   if (!file) {
      return 0;
   }

   fseek(file, 32, SEEK_SET);
   while (fread(header, sizeof(header), 1, file) == 1) {
      if (header[4] == 14 && header[7] == 0) {
         GX2ComputeShader serialized;
         *metadata = malloc(header[5]);
         if (!*metadata || fread(*metadata, header[5], 1, file) != 1) {
            break;
         }
         memcpy(&serialized, *metadata, sizeof(serialized));
         if (serialized.initialValueCount || serialized.samplerVarCount) {
            break;
         }
         *shader               = serialized;
         shader->program       = NULL;
         shader->uniformBlocks = shader->uniformBlockCount ? (GX2UniformBlock *)(*metadata + ((uint32_t)serialized.uniformBlocks & 0xfffff)) : NULL;
         shader->uniformVars   = shader->uniformVarCount ? (GX2UniformVar *)(*metadata + ((uint32_t)serialized.uniformVars & 0xfffff)) : NULL;
         shader->loopVars      = shader->loopVarCount ? (GX2LoopVar *)(*metadata + ((uint32_t)serialized.loopVars & 0xfffff)) : NULL;
         for (unsigned i = 0; i < shader->uniformBlockCount; ++i) {
            shader->uniformBlocks[i].name = (const char *)(*metadata +
                                                           ((uint32_t)shader->uniformBlocks[i].name & 0xfffff));
         }
         for (unsigned i = 0; i < shader->uniformVarCount; ++i) {
            shader->uniformVars[i].name = (const char *)(*metadata +
                                                         ((uint32_t)shader->uniformVars[i].name & 0xfffff));
         }
      } else if (header[4] == 15 && header[7] == 0) {
         shader->program = memalign(256, header[5]);
         if (shader->program) {
            result = fread(shader->program, header[5], 1, file) == 1;
         }
         break;
      } else if (header[4] == 1) {
         break;
      } else {
         fseek(file, header[5], SEEK_CUR);
      }
   }
   fclose(file);
   return result;
}

int
main(int argc, char **argv)
{
   GX2ComputeShader shader = {0};
   uint8_t *metadata       = NULL;
   char path[512]          = "SD card not mounted";
   unsigned mismatches     = 0;
   int result              = 1;

   WHBProcInit();
   WHBLogCafeInit();
   void *commands        = memalign(256, 4 * 1024 * 1024);
   uint32_t attributes[] = {
      GX2_INIT_CMD_BUF_BASE, (uint32_t)commands,
      GX2_INIT_CMD_BUF_POOL_SIZE, 4 * 1024 * 1024,
      GX2_INIT_END};
   GX2Init(attributes);
   if (WHBMountSdCard()) {
      snprintf(path, sizeof(path), "%s/wiiu/apps/cafeglsl-compute/simple.gsh",
               WHBGetSdCardMountPath());
      if (argc > 1) {
         snprintf(path, sizeof(path), "%s", argv[1]);
      }
      if (LoadShader(path, &shader, &metadata)) {
         unsigned localInvocations                              = shader.workgroupSizeX * shader.workgroupSizeY * shader.workgroupSizeZ;
         unsigned waves                                         = (localInvocations + 63) / 64;
         uint32_t nativeGroups[4] __attribute__((aligned(256))) = {
            sGroups[0] * waves, sGroups[1], sGroups[2], 0};
         for (unsigned i = 0; i < shader.uniformVarCount; ++i) {
            const GX2UniformVar *uniform = &shader.uniformVars[i];
            if (!strcmp(uniform->name, "count")) {
               sParameters[uniform->offset] = __builtin_bswap32(sElementCount);
            } else if (!strcmp(uniform->name, "multiplier")) {
               sParameters[uniform->offset] = __builtin_bswap32(sMultiplier);
            } else if (!strcmp(uniform->name, "offset")) {
               sParameters[uniform->offset] = __builtin_bswap32(sOffset);
            }
         }
         for (unsigned i = 0; i < sizeof(sOutput) / sizeof(sOutput[0]); ++i) {
            sOutput[i] = __builtin_bswap32(0xdeadbeef);
            sInput[i]  = __builtin_bswap32(0x90203040u ^ (17u * i + 13u));
         }
         DCFlushRange(sOutput, sizeof(sOutput));
         DCFlushRange(nativeGroups, sizeof(nativeGroups));
         GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, shader.program, shader.size);
         GX2SetShaderMode(GX2_SHADER_MODE_COMPUTE_SHADER);
         GX2SetComputeShader(&shader);
         GX2Invalidate(GX2_INVALIDATE_MODE_CPU | GX2_INVALIDATE_MODE_UNIFORM_BLOCK,
                       sParameters, sizeof(sParameters));
         for (unsigned i = 0; i < shader.uniformBlockCount; ++i) {
            GX2SetComputeUniformBlock(shader.uniformBlocks[i].offset, sizeof(sParameters), sParameters);
         }
         GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, sInput, sizeof(sInput));
         GX2SetAttribBuffer(1, sizeof(sInput), sizeof(sInput[0]), sInput);
         GX2SetShaderExportBuffer(sOutput, sizeof(sOutput));
         GX2DispatchCompute(nativeGroups);
         GX2Invalidate(GX2_INVALIDATE_MODE_EXPORT_BUFFER, sOutput, sizeof(sOutput));
         GX2DrawDone();
         DCInvalidateRange(sOutput, sizeof(sOutput));
         unsigned invocations = localInvocations * sGroups[0] * sGroups[1] * sGroups[2];
         for (unsigned i = 0; i < sizeof(sOutput) / sizeof(sOutput[0]); ++i) {
            sOutput[i]        = __builtin_bswap32(sOutput[i]);
            uint32_t expected = 0xdeadbeef;
            if (i < invocations && i < sElementCount) {
               expected = __builtin_bswap32(sInput[i]) * sMultiplier + sOffset;
            }
            mismatches += sOutput[i] != expected;
         }
         GX2SetShaderExportBuffer(NULL, 0);
         GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
         GX2DrawDone();
         result = 0;
      }
      WHBUnmountSdCard();
   }
   free(shader.program);
   free(metadata);

   GX2Shutdown();
   free(commands);
   WHBLogConsoleInit();
   WHBLogPrintf("CafeGLSL compute: %s", path);
   if (result) {
      WHBLogPrintf("Could not load the compute shader.");
   } else {
      WHBLogPrintf("Workgroup: %u,%u,%u", shader.workgroupSizeX, shader.workgroupSizeY, shader.workgroupSizeZ);
      WHBLogPrintf("Dispatch: %u,%u,%u", sGroups[0], sGroups[1], sGroups[2]);
      WHBLogPrintf("Parameters: count=%u, multiplier=%u, offset=%u", sElementCount, sMultiplier, sOffset);
      WHBLogPrintf("%s: %u mismatches", mismatches ? "FAIL" : "PASS", mismatches);
      for (unsigned i = 0; i < 64; i += 8) {
         WHBLogPrintf("%2u: %u %u %u %u %u %u %u %u", i,
                      sOutput[i], sOutput[i + 1], sOutput[i + 2], sOutput[i + 3],
                      sOutput[i + 4], sOutput[i + 5], sOutput[i + 6], sOutput[i + 7]);
      }
   }
   WHBLogPrintf("Press HOME to exit.");
   while (WHBProcIsRunning()) {
      WHBLogConsoleDraw();
      OSSleepTicks(OSMillisecondsToTicks(50));
   }
   WHBLogConsoleFree();
   WHBLogCafeDeinit();
   WHBProcShutdown();
   return result || mismatches;
}
