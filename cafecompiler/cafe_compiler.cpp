#include "cafe_compiler.h"

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/nir/nir_shader_compiler_options.h"
#include "compiler/glsl/glsl_parser_extras.h"
#include "compiler/glsl/ir.h"
#include "compiler/glsl/builtin_functions.h"
#include "compiler/glsl/gl_nir.h"
#include "compiler/glsl/gl_nir_linker.h"
#include "compiler/glsl/linker_util.h"
#include "compiler/glsl/standalone_scaffolding.h"
#include "compiler/glsl_types.h"
#include "gallium/auxiliary/tgsi/tgsi_from_mesa.h"
#define S_FIXED R600_S_FIXED
#include "gallium/drivers/r600/r600_asm.h"
#include "gallium/drivers/r600/r600_isa.h"
#include "gallium/drivers/r600/r600_pipe.h"
#include "gallium/drivers/r600/r600_sfn.h"
#include "gallium/drivers/r600/r600_shader.h"
#include "gallium/drivers/r600/r600_shader_common.h"
#include "gallium/drivers/r600/r600d.h"
#include "gallium/drivers/r600/sfn/sfn_nir.h"
#undef S_FIXED
#include "main/mtypes.h"
#include "main/shader_types.h"
#include "program/prog_parameter.h"
#include "util/bitscan.h"
#include "util/list.h"
#include "util/macros.h"
#include "util/u_endian.h"
#include "util/u_memory.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

constexpr unsigned kMaxUniformBlocks = 16;
constexpr unsigned kMaxAluConstComponents = 1024;
constexpr unsigned kMaxSamplers = 18;
constexpr unsigned kLatteResourceBase = 0x80;
constexpr const char *kDefaultUniformBlockName = "__cafe_loose_uniforms";
constexpr unsigned kDefaultUniformBlockBinding = 0;

struct UniformBlockInfo {
   std::string name;
   uint32_t binding;
   uint32_t size;
};

struct UniformInfo {
   std::string name;
   GX2ShaderVarType type;
   uint32_t count;
   uint32_t offset;
   int32_t block;
};

struct SamplerInfo {
   std::string name;
   GX2SamplerVarType type;
   uint32_t binding;
};

struct AttributeInfo {
   std::string name;
   GX2ShaderVarType type;
   uint32_t count;
   uint32_t location;
};

struct ReflectionInfo {
   std::vector<UniformBlockInfo> blocks;
   std::vector<UniformInfo> uniforms;
   std::vector<SamplerInfo> samplers;
   std::vector<AttributeInfo> attributes;
};

static unsigned UnpackedUniformSize(const glsl_type *type, bool bindless)
{
   return glsl_count_vec4_slots(type, false, bindless);
}

static void InitializeR600NirOptions(nir_shader_compiler_options &options)
{
   r600_init_nir_options(&options, R700, CHIP_RV730);

   /* Upstream turns non-block uniforms into a UBO; we decide per shader instead (see
    * PrepareNir). The only field we differ from the driver's table on.
    */
   options.lower_uniforms_to_ubo = false;
}

static int LookupParameterIndex(gl_program *program, nir_variable *variable)
{
   gl_program_parameter_list *parameters = program->Parameters;
   for (unsigned i = 0; i < parameters->NumParameters; ++i) {
      if (variable->data.location >= 0 &&
          parameters->Parameters[i].MainUniformStorageIndex ==
             static_cast<unsigned>(variable->data.location))
         return i;
   }

   if (!program->sh.data->spirv) {
      const size_t name_length = strlen(variable->name);
      for (unsigned i = 0; i < parameters->NumParameters; ++i) {
         const gl_program_parameter &parameter = parameters->Parameters[i];
         if (!strncmp(parameter.Name, variable->name, name_length) &&
             (parameter.Name[name_length] == '.' ||
              parameter.Name[name_length] == '['))
            return i;
      }
   }

   return -1;
}

static void AssignUniformLocations(gl_program *program, nir_shader *nir)
{
   int sampler_index = 0;
   int image_index = 0;

   nir_foreach_variable_with_modes(variable, nir, nir_var_uniform | nir_var_image) {
      const glsl_type *type = glsl_without_array(variable->type);
      int location;

      if (!variable->data.bindless &&
          (glsl_type_is_sampler(type) || glsl_type_is_image(type))) {
         const unsigned slots = glsl_count_attribute_slots(variable->type, false);
         if (glsl_type_is_sampler(type)) {
            location = sampler_index;
            sampler_index += slots;
         } else {
            location = image_index;
            image_index += slots;
         }
      } else if (variable->state_slots) {
         continue;
      } else {
         location = LookupParameterIndex(program, variable);
      }

      variable->data.driver_location = location;
   }
}

static unsigned SamplerArraySize(const glsl_type *type)
{
   return glsl_type_is_array(type) ? glsl_get_aoa_size(type) : 1;
}

static bool AssignSamplerBindings(nir_shader *nir,
                            std::unordered_map<std::string, unsigned> &bindings,
                            std::string &diagnostics)
{
   std::array<bool, kMaxSamplers> used{};

   nir_foreach_variable_with_modes(variable, nir, nir_var_uniform) {
      const glsl_type *type = glsl_without_array(variable->type);
      if (glsl_contains_sampler(variable->type) && !glsl_type_is_sampler(type)) {
         diagnostics = "Samplers inside structures are not supported yet";
         return false;
      }
      if (!glsl_type_is_sampler(type) || !variable->data.explicit_binding)
         continue;

      const unsigned count = SamplerArraySize(variable->type);
      if (variable->data.binding + count > kMaxSamplers) {
         diagnostics = "Sampler binding exceeds Latte's 18 sampler slots";
         return false;
      }
      for (unsigned i = 0; i < count; ++i) {
         if (used[variable->data.binding + i]) {
            diagnostics = "Overlapping explicit sampler bindings";
            return false;
         }
         used[variable->data.binding + i] = true;
      }
   }

   nir_foreach_variable_with_modes(variable, nir, nir_var_uniform) {
      const glsl_type *type = glsl_without_array(variable->type);
      if (!glsl_type_is_sampler(type))
         continue;

      const unsigned count = SamplerArraySize(variable->type);
      if (!variable->data.explicit_binding) {
         unsigned binding = 0;
         for (; binding + count <= kMaxSamplers; ++binding) {
            bool available = true;
            for (unsigned i = 0; i < count; ++i)
               available &= !used[binding + i];
            if (available)
               break;
         }
         if (binding + count > kMaxSamplers) {
            diagnostics = "No free Latte sampler binding range";
            return false;
         }
         variable->data.binding = binding;
         variable->data.explicit_binding = true;
         for (unsigned i = 0; i < count; ++i)
            used[binding + i] = true;
      }

      bindings[variable->name] = variable->data.binding;
   }

   return true;
}

struct UboRemapState {
   const std::vector<unsigned> *bindings;
   bool valid = true;
   bool progress = false;
};

static bool RemapUboInstruction(nir_builder *builder, nir_instr *instruction, void *data)
{
   UboRemapState &state = *static_cast<UboRemapState *>(data);
   if (instruction->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrinsic = nir_instr_as_intrinsic(instruction);
   if (intrinsic->intrinsic != nir_intrinsic_load_ubo)
      return false;

   if (!nir_src_is_const(intrinsic->src[0])) {
      state.valid = false;
      return false;
   }

   const unsigned old_index = nir_src_as_uint(intrinsic->src[0]);
   if (old_index >= state.bindings->size()) {
      state.valid = false;
      return false;
   }

   builder->cursor = nir_before_instr(instruction);
   nir_src_rewrite(
      &intrinsic->src[0], nir_imm_int(builder, (*state.bindings)[old_index]));
   state.progress = true;
   return true;
}

static bool RemapUbos(nir_shader *nir,
               const std::vector<unsigned> &bindings,
               std::string &diagnostics)
{
   UboRemapState state{&bindings};
   nir_shader_instructions_pass(nir,
                                RemapUboInstruction,
                                nir_metadata_control_flow,
                                &state);
   if (!state.valid) {
      diagnostics = "Dynamic or invalid UBO indexing is not supported yet";
      return false;
   }

   return true;
}

static GX2ShaderVarType ConvertType(const glsl_type *raw_type)
{
   const glsl_type *type = glsl_without_array(raw_type);
   const glsl_base_type base = glsl_get_base_type(type);
   const unsigned rows = glsl_get_vector_elements(type);
   const unsigned columns = glsl_get_matrix_columns(type);

   if (columns > 1) {
      static constexpr GX2ShaderVarType float_matrix_types[3][3] = {
         {GX2_SHADER_VAR_TYPE_FLOAT2X2, GX2_SHADER_VAR_TYPE_FLOAT2X3,
          GX2_SHADER_VAR_TYPE_FLOAT2X4},
         {GX2_SHADER_VAR_TYPE_FLOAT3X2, GX2_SHADER_VAR_TYPE_FLOAT3X3,
          GX2_SHADER_VAR_TYPE_FLOAT3X4},
         {GX2_SHADER_VAR_TYPE_FLOAT4X2, GX2_SHADER_VAR_TYPE_FLOAT4X3,
          GX2_SHADER_VAR_TYPE_FLOAT4X4},
      };
      static constexpr GX2ShaderVarType double_matrix_types[3][3] = {
         {GX2_SHADER_VAR_TYPE_DOUBLE2X2, GX2_SHADER_VAR_TYPE_DOUBLE2X3,
          GX2_SHADER_VAR_TYPE_DOUBLE2X4},
         {GX2_SHADER_VAR_TYPE_DOUBLE3X2, GX2_SHADER_VAR_TYPE_DOUBLE3X3,
          GX2_SHADER_VAR_TYPE_DOUBLE3X4},
         {GX2_SHADER_VAR_TYPE_DOUBLE4X2, GX2_SHADER_VAR_TYPE_DOUBLE4X3,
          GX2_SHADER_VAR_TYPE_DOUBLE4X4},
      };
      if (columns >= 2 && columns <= 4 && rows >= 2 && rows <= 4) {
         if (base == GLSL_TYPE_FLOAT)
            return float_matrix_types[columns - 2][rows - 2];
         if (base == GLSL_TYPE_DOUBLE)
            return double_matrix_types[columns - 2][rows - 2];
      }
      return GX2_SHADER_VAR_TYPE_VOID;
   }

   if (rows < 1 || rows > 4)
      return GX2_SHADER_VAR_TYPE_VOID;

   static constexpr GX2ShaderVarType float_types[] = {
      GX2_SHADER_VAR_TYPE_FLOAT, GX2_SHADER_VAR_TYPE_FLOAT2,
      GX2_SHADER_VAR_TYPE_FLOAT3, GX2_SHADER_VAR_TYPE_FLOAT4};
   static constexpr GX2ShaderVarType int_types[] = {
      GX2_SHADER_VAR_TYPE_INT, GX2_SHADER_VAR_TYPE_INT2,
      GX2_SHADER_VAR_TYPE_INT3, GX2_SHADER_VAR_TYPE_INT4};
   static constexpr GX2ShaderVarType uint_types[] = {
      GX2_SHADER_VAR_TYPE_UINT, GX2_SHADER_VAR_TYPE_UINT2,
      GX2_SHADER_VAR_TYPE_UINT3, GX2_SHADER_VAR_TYPE_UINT4};
   static constexpr GX2ShaderVarType bool_types[] = {
      GX2_SHADER_VAR_TYPE_BOOL, GX2_SHADER_VAR_TYPE_BOOL2,
      GX2_SHADER_VAR_TYPE_BOOL3, GX2_SHADER_VAR_TYPE_BOOL4};
   static constexpr GX2ShaderVarType double_types[] = {
      GX2_SHADER_VAR_TYPE_DOUBLE, GX2_SHADER_VAR_TYPE_DOUBLE2,
      GX2_SHADER_VAR_TYPE_DOUBLE3, GX2_SHADER_VAR_TYPE_DOUBLE4};

   switch (base) {
   case GLSL_TYPE_FLOAT: return float_types[rows - 1];
   case GLSL_TYPE_INT: return int_types[rows - 1];
   case GLSL_TYPE_UINT: return uint_types[rows - 1];
   case GLSL_TYPE_BOOL: return bool_types[rows - 1];
   case GLSL_TYPE_DOUBLE: return double_types[rows - 1];
   default: return GX2_SHADER_VAR_TYPE_VOID;
   }
}

static GX2SamplerVarType ConvertSamplerType(const glsl_type *raw_type)
{
   switch (glsl_get_sampler_dim(glsl_without_array(raw_type))) {
   case GLSL_SAMPLER_DIM_1D: return GX2_SAMPLER_VAR_TYPE_SAMPLER_1D;
   case GLSL_SAMPLER_DIM_3D: return GX2_SAMPLER_VAR_TYPE_SAMPLER_3D;
   case GLSL_SAMPLER_DIM_CUBE: return GX2_SAMPLER_VAR_TYPE_SAMPLER_CUBE;
   default: return GX2_SAMPLER_VAR_TYPE_SAMPLER_2D;
   }
}

static std::unordered_map<std::string, unsigned>::const_iterator
FindSamplerBinding(const std::unordered_map<std::string, unsigned> &bindings,
                   const char *name)
{
   auto binding = bindings.find(name);
   if (binding != bindings.end())
      return binding;

   std::string base_name(name);
   if (base_name.size() >= 3 &&
       base_name.compare(base_name.size() - 3, 3, "[0]") == 0) {
      base_name.resize(base_name.size() - 3);
      return bindings.find(base_name);
   }
   return bindings.end();
}

static int FindParameterOffset(const gl_program *program, unsigned uniform_index)
{
   if (!program->Parameters)
      return -1;
   for (unsigned i = 0; i < program->Parameters->NumParameters; ++i) {
      const gl_program_parameter &parameter = program->Parameters->Parameters[i];
      if (parameter.UniformStorageIndex == uniform_index)
         return parameter.ValueOffset * sizeof(gl_constant_value);
   }
   return -1;
}

static bool BuildReflection(gl_shader_program *shader_program,
                     gl_program *program,
                     mesa_shader_stage stage,
                     const std::unordered_map<std::string, unsigned> &sampler_bindings,
                     bool lower_loose_uniforms,
                     ReflectionInfo &reflection,
                     std::string &diagnostics)
{
   std::vector<int> block_to_output(program->info.num_ubos, -1);

   for (unsigned i = 0; i < shader_program->data->NumProgramResourceList; ++i) {
      gl_program_resource *resource = &shader_program->data->ProgramResourceList[i];
      if (resource->Type != GL_UNIFORM_BLOCK)
         continue;

      const gl_uniform_block *block = static_cast<const gl_uniform_block *>(resource->Data);
      const char *name = block->name.string;
      if (!name)
         continue;
      if (block->Binding >= kMaxUniformBlocks) {
         diagnostics = "Uniform block binding exceeds Latte's 16 uniform-block slots";
         return false;
      }

      const int output_index = static_cast<int>(reflection.blocks.size());
      reflection.blocks.push_back({name, block->Binding, block->UniformBufferSize});

      for (unsigned block_index = 0; block_index < program->info.num_ubos; ++block_index) {
         if (program->sh.UniformBlocks[block_index] == block)
            block_to_output[block_index] = output_index;
      }
   }

   const gl_uniform_storage *uniform_base = shader_program->data->UniformStorage;

   for (unsigned i = 0; i < shader_program->data->NumProgramResourceList; ++i) {
      gl_program_resource *resource = &shader_program->data->ProgramResourceList[i];
      if (resource->Type == GL_PROGRAM_INPUT && stage == MESA_SHADER_VERTEX) {
         const gl_shader_variable *variable =
            static_cast<const gl_shader_variable *>(resource->Data);
         const char *name = variable->name.string;
         if (!name)
            continue;
         const GX2ShaderVarType type = ConvertType(variable->type);
         if (type == GX2_SHADER_VAR_TYPE_VOID) {
            diagnostics = std::string("Vertex attribute type for '") + name +
                          "' cannot be represented by GX2";
            return false;
         }
         reflection.attributes.push_back(
            {name, type, glsl_type_is_array(variable->type) ?
                         glsl_get_aoa_size(variable->type) : 1,
             static_cast<uint32_t>(variable->location)});
         continue;
      }

      if (resource->Type != GL_UNIFORM && resource->Type != GL_BUFFER_VARIABLE)
         continue;

      const gl_uniform_storage *uniform =
         static_cast<const gl_uniform_storage *>(resource->Data);
      const char *name = uniform->name.string;
      if (!name)
         continue;
      const glsl_type *type = glsl_without_array(uniform->type);

      if (glsl_type_is_image(type) || glsl_type_is_texture(type))
         continue;
      if (glsl_type_is_sampler(type)) {
         auto binding = FindSamplerBinding(sampler_bindings, name);
         if (binding != sampler_bindings.end())
            reflection.samplers.push_back(
               {name, ConvertSamplerType(uniform->type), binding->second});
         continue;
      }

      const GX2ShaderVarType gx2_type = ConvertType(uniform->type);
      if (gx2_type == GX2_SHADER_VAR_TYPE_VOID) {
         diagnostics = std::string("Uniform type for '") + name +
                       "' cannot be represented by GX2";
         return false;
      }

      const unsigned uniform_index = static_cast<unsigned>(uniform - uniform_base);
      const uint32_t count = uniform->array_elements ? uniform->array_elements : 1;
      if (uniform->block_index >= 0) {
         const unsigned block_index = static_cast<unsigned>(uniform->block_index);
         if (block_index < block_to_output.size() && block_to_output[block_index] >= 0) {
            reflection.uniforms.push_back(
               {name, gx2_type, count, static_cast<uint32_t>(uniform->offset),
                block_to_output[block_index]});
         }
      } else {
         const int offset = FindParameterOffset(program, uniform_index);
         if (offset >= 0) {
            reflection.uniforms.push_back(
               {name, gx2_type, count, static_cast<uint32_t>(offset), -1});
         }
      }
   }

   /* The parameter list already pads each uniform to a vec4, so offsets carry over. */
   if (lower_loose_uniforms) {
      const int loose_block = static_cast<int>(reflection.blocks.size());
      const uint32_t size = program->Parameters ?
         ((program->Parameters->NumParameterValues + 3) & ~3u) * sizeof(uint32_t) : 0;
      reflection.blocks.push_back(
         {kDefaultUniformBlockName, kDefaultUniformBlockBinding, size});
      for (UniformInfo &uniform : reflection.uniforms) {
         if (uniform.block < 0)
            uniform.block = loose_block;
      }
   }

   return true;
}

static char *DuplicateName(const std::string &name)
{
   return strdup(name.c_str());
}

template <typename Shader>
static bool CopyReflection(const ReflectionInfo &reflection, Shader *shader)
{
   shader->uniformBlockCount = reflection.blocks.size();
   shader->uniformBlocks = static_cast<GX2UniformBlock *>(
      calloc(reflection.blocks.size(), sizeof(GX2UniformBlock)));
   shader->uniformVarCount = reflection.uniforms.size();
   shader->uniformVars = static_cast<GX2UniformVar *>(
      calloc(reflection.uniforms.size(), sizeof(GX2UniformVar)));
   shader->samplerVarCount = reflection.samplers.size();
   shader->samplerVars = static_cast<GX2SamplerVar *>(
      calloc(reflection.samplers.size(), sizeof(GX2SamplerVar)));
   shader->loopVarCount = 1;
   shader->loopVars = static_cast<GX2LoopVar *>(calloc(1, sizeof(GX2LoopVar)));

   if ((!reflection.blocks.empty() && !shader->uniformBlocks) ||
       (!reflection.uniforms.empty() && !shader->uniformVars) ||
       (!reflection.samplers.empty() && !shader->samplerVars) ||
       !shader->loopVars)
      return false;

   for (unsigned i = 0; i < reflection.blocks.size(); ++i) {
      shader->uniformBlocks[i].name = DuplicateName(reflection.blocks[i].name);
      if (!shader->uniformBlocks[i].name)
         return false;
      shader->uniformBlocks[i].offset = reflection.blocks[i].binding;
      shader->uniformBlocks[i].size = reflection.blocks[i].size;
   }
   for (unsigned i = 0; i < reflection.uniforms.size(); ++i) {
      shader->uniformVars[i].name = DuplicateName(reflection.uniforms[i].name);
      if (!shader->uniformVars[i].name)
         return false;
      shader->uniformVars[i].type = reflection.uniforms[i].type;
      shader->uniformVars[i].count = reflection.uniforms[i].count;
      shader->uniformVars[i].offset = reflection.uniforms[i].offset;
      shader->uniformVars[i].block = reflection.uniforms[i].block;
   }
   for (unsigned i = 0; i < reflection.samplers.size(); ++i) {
      shader->samplerVars[i].name = DuplicateName(reflection.samplers[i].name);
      if (!shader->samplerVars[i].name)
         return false;
      shader->samplerVars[i].type = reflection.samplers[i].type;
      shader->samplerVars[i].location = reflection.samplers[i].binding;
   }

   shader->loopVars[0].offset = 0;
   shader->loopVars[0].value = 0x01000fff;
   return true;
}

static bool CopyAttributes(const ReflectionInfo &reflection, GX2VertexShader *shader)
{
   shader->attribVarCount = reflection.attributes.size();
   shader->attribVars = static_cast<GX2AttribVar *>(
      calloc(reflection.attributes.size(), sizeof(GX2AttribVar)));
   if (!reflection.attributes.empty() && !shader->attribVars)
      return false;

   for (unsigned i = 0; i < reflection.attributes.size(); ++i) {
      shader->attribVars[i].name = DuplicateName(reflection.attributes[i].name);
      if (!shader->attribVars[i].name)
         return false;
      shader->attribVars[i].type = reflection.attributes[i].type;
      shader->attribVars[i].count = reflection.attributes[i].count;
      shader->attribVars[i].location = reflection.attributes[i].location;
   }
   return true;
}

static unsigned LatteSemantic(gl_varying_slot slot)
{
   unsigned name;
   unsigned index;
   tgsi_get_gl_varying_semantic(slot, true, &name, &index);

   if (name == TGSI_SEMANTIC_GENERIC)
      index += 9;
   else if (name == TGSI_SEMANTIC_PCOORD)
      index = 8;

   switch (name) {
   case TGSI_SEMANTIC_POSITION:
   case TGSI_SEMANTIC_PSIZE:
   case TGSI_SEMANTIC_EDGEFLAG:
   case TGSI_SEMANTIC_FACE:
   case TGSI_SEMANTIC_SAMPLEMASK:
   case TGSI_SEMANTIC_CLIPVERTEX:
      return 0;
   case TGSI_SEMANTIC_GENERIC:
   case TGSI_SEMANTIC_TEXCOORD:
   case TGSI_SEMANTIC_PCOORD:
      return index + 1;
   default:
      return (0x80 | (name << 3) | index) + 1;
   }
}

static bool BuildVertexSemanticMap(nir_shader *nir,
                            std::array<unsigned, 32> &semantics,
                            std::string &diagnostics)
{
   semantics.fill(0xff);

   /* GX2InitFetchShaderEx delivers an attribute to R(location + 1), and the SFN
    * backend puts input base N in R(N + 1), so the bases have to equal the declared
    * locations. gl_nir_lower_optimize_varyings() packs them into a prefix sum over the
    * locations that survived DCE instead, so undo that. The semantic table stays an
    * identity map with a hole for every location the shader does not read, which drops
    * unused streams rather than landing them on a live register.
    */
   unsigned num_inputs = 0;
   nir_foreach_block(block, nir_shader_get_entrypoint(nir)) {
      nir_foreach_instr(instr, block) {
         nir_variable_mode mode;
         nir_intrinsic_instr *load =
            nir_get_io_intrinsic(instr, nir_var_shader_in, &mode);
         if (!load)
            continue;

         const nir_io_semantics io = nir_intrinsic_io_semantics(load);
         const int location = static_cast<int>(io.location) - VERT_ATTRIB_GENERIC0;
         if (location < 0 ||
             static_cast<unsigned>(location) + io.num_slots > semantics.size()) {
            diagnostics = "Vertex attribute location exceeds Latte's 32 slots";
            return false;
         }
         if (io.high_dvec2) {
            diagnostics = "64-bit vertex attributes cannot be represented by GX2";
            return false;
         }

         nir_intrinsic_set_base(load, location);
         for (unsigned i = 0; i < io.num_slots; ++i)
            semantics[location + i] = location + i;
         num_inputs = MAX2(num_inputs, static_cast<unsigned>(location) + io.num_slots);
      }
   }

   /* Unused once the loads are lowered, but a re-lowering would read them. */
   nir_foreach_variable_with_modes(variable, nir, nir_var_shader_in)
      variable->data.driver_location =
         MAX2(0, variable->data.location - VERT_ATTRIB_GENERIC0);

   nir->num_inputs = num_inputs;
   return true;
}

static void FillVertexRegisters(const r600_pipe_shader &pipe_shader,
                         const std::array<unsigned, 32> &semantics,
                         GX2VertexShader &output)
{
   const r600_shader &shader = pipe_shader.shader;
   output.regs.sq_pgm_resources_vs =
      S_028868_NUM_GPRS(shader.bc.ngpr) |
      S_028868_STACK_SIZE(shader.bc.nstack);
   output.regs.vgt_primitiveid_en = 0;

   for (uint32_t &value : output.regs.spi_vs_out_id)
      value = 0xffffffff;

   for (unsigned i = 0; i < shader.noutput; ++i) {
      const r600_shader_io &io = shader.output[i];
      if (io.export_param < 0)
         continue;
      const unsigned semantic = 0x80 | LatteSemantic(io.varying_slot);
      uint32_t &word = output.regs.spi_vs_out_id[io.export_param / 4];
      const unsigned shift = (io.export_param & 3) * 8;
      word = (word & ~(0xffu << shift)) | (semantic << shift);
   }

   output.regs.spi_vs_out_config =
      S_0286C4_VS_EXPORT_COUNT(shader.highest_export_param);
   output.regs.num_spi_vs_out_id =
      MAX2(1u, DIV_ROUND_UP(shader.highest_export_param + 1, 4));
   output.regs.pa_cl_vs_out_cntl =
      S_02881C_VS_OUT_CCDIST0_VEC_ENA((shader.clip_dist_write & 0x0f) != 0) |
      S_02881C_VS_OUT_CCDIST1_VEC_ENA((shader.clip_dist_write & 0xf0) != 0) |
      S_02881C_VS_OUT_MISC_VEC_ENA(shader.vs_out_misc_write) |
      S_02881C_USE_VTX_POINT_SIZE(shader.vs_out_point_size) |
      S_02881C_USE_VTX_EDGE_FLAG(shader.vs_out_edgeflag) |
      S_02881C_USE_VTX_RENDER_TARGET_INDX(shader.vs_out_layer) |
      S_02881C_USE_VTX_VIEWPORT_INDX(shader.vs_out_viewport);

   output.regs.sq_vtx_semantic_clear = 0xffffffff;
   std::fill(std::begin(output.regs.sq_vtx_semantic),
             std::end(output.regs.sq_vtx_semantic), 0xff);
   unsigned semantic_count = 0;
   for (unsigned i = 0; i < shader.ninput; ++i) {
      const r600_shader_io &io = shader.input[i];
      if (io.gpr < 1 || io.gpr > 32)
         continue;
      const unsigned semantic = semantics[io.gpr - 1];
      output.regs.sq_vtx_semantic[io.gpr - 1] =
         semantic == 0xff ? io.gpr - 1 : semantic;
      semantic_count = MAX2(semantic_count, io.gpr);
   }
   output.regs.num_sq_vtx_semantic = semantic_count;
   output.regs.vgt_strmout_buffer_en = pipe_shader.enabled_stream_buffers_mask;
   output.regs.vgt_vertex_reuse_block_cntl = shader.highest_export_param + 1 > 21 ? 2 : 14;
   output.regs.vgt_hos_reuse_depth = shader.highest_export_param + 1 > 21 ? 4 : 16;
}

static void FillPixelRegisters(const r600_pipe_shader &pipe_shader, GX2PixelShader &output)
{
   const r600_shader &shader = pipe_shader.shader;
   int position_index = -1;
   int face_index = -1;
   int sample_index = -1;
   bool need_linear = false;

   for (unsigned i = 0; i < shader.ninput; ++i) {
      const r600_shader_io &input = shader.input[i];
      if (input.varying_slot == VARYING_SLOT_POS)
         position_index = i;
      if (input.varying_slot == VARYING_SLOT_FACE && face_index < 0)
         face_index = i;
      if (input.system_value == SYSTEM_VALUE_SAMPLE_ID)
         sample_index = i;
      need_linear |= input.interpolate == TGSI_INTERPOLATE_LINEAR;
   }

   output.regs.sq_pgm_resources_ps =
      S_028850_NUM_GPRS(shader.bc.ngpr) |
      S_028850_DX10_CLAMP(1) |
      S_028850_STACK_SIZE(shader.bc.nstack);

   unsigned exports = 0;
   for (unsigned i = 0; i < shader.noutput; ++i) {
      if (shader.output[i].frag_result == FRAG_RESULT_DEPTH ||
          shader.output[i].frag_result == FRAG_RESULT_STENCIL ||
          shader.output[i].frag_result == FRAG_RESULT_SAMPLE_MASK)
         exports |= 1;
   }
   exports |= S_028854_EXPORT_COLORS(shader.nr_ps_color_exports);
   output.regs.sq_pgm_exports_ps = exports ? exports : 2;

   output.regs.spi_ps_in_control_0 =
      S_0286CC_NUM_INTERP(shader.ninput) |
      S_0286CC_PERSP_GRADIENT_ENA(1) |
      S_0286CC_LINEAR_GRADIENT_ENA(need_linear);
   if (position_index >= 0) {
      const r600_shader_io &position = shader.input[position_index];
      output.regs.spi_ps_in_control_0 |=
         S_0286CC_POSITION_ENA(1) |
         S_0286CC_POSITION_CENTROID(
            position.interpolate_location == TGSI_INTERPOLATE_LOC_CENTROID) |
         S_0286CC_POSITION_ADDR(position.gpr) |
         S_0286CC_BARYC_SAMPLE_CNTL(1) |
         S_0286CC_POSITION_SAMPLE(
            position.interpolate_location == TGSI_INTERPOLATE_LOC_SAMPLE);
   }

   if (face_index >= 0) {
      output.regs.spi_ps_in_control_1 |=
         S_0286D0_FRONT_FACE_ENA(1) |
         S_0286D0_FRONT_FACE_ADDR(shader.input[face_index].gpr);
   }
   if (sample_index >= 0) {
      output.regs.spi_ps_in_control_1 |=
         S_0286D0_FIXED_PT_POSITION_ENA(1) |
         S_0286D0_FIXED_PT_POSITION_ADDR(shader.input[sample_index].gpr);
   }

   for (unsigned i = 0; i < shader.ninput && i < 32; ++i) {
      const r600_shader_io &input = shader.input[i];
      unsigned value = S_028644_SEMANTIC(
          0x80 | LatteSemantic(input.varying_slot));
      if (input.varying_slot == VARYING_SLOT_COL0)
         value |= S_028644_DEFAULT_VAL(3);
      if (input.varying_slot == VARYING_SLOT_POS ||
          input.interpolate == TGSI_INTERPOLATE_CONSTANT)
         value |= S_028644_FLAT_SHADE(1);
      if (input.varying_slot == VARYING_SLOT_PNTC)
         value |= S_028644_PT_SPRITE_TEX(1);
      if (input.interpolate_location == TGSI_INTERPOLATE_LOC_CENTROID)
         value |= S_028644_SEL_CENTROID(1);
      if (input.interpolate_location == TGSI_INTERPOLATE_LOC_SAMPLE)
         value |= S_028644_SEL_SAMPLE(1);
      if (input.interpolate == TGSI_INTERPOLATE_LINEAR)
         value |= S_028644_SEL_LINEAR(1);
      output.regs.spi_ps_input_cntls[i] = value;
   }
   output.regs.num_spi_ps_input_cntl = shader.ninput;
   output.regs.cb_shader_mask = shader.ps_color_export_mask;
   output.regs.cb_shader_control =
      S_028808_MULTIWRITE_ENABLE(shader.nr_ps_color_exports > 1) | 1;

   bool depth_export = false;
   bool stencil_export = false;
   bool mask_export = false;
   for (unsigned i = 0; i < shader.noutput; ++i) {
      depth_export |= shader.output[i].frag_result == FRAG_RESULT_DEPTH;
      stencil_export |= shader.output[i].frag_result == FRAG_RESULT_STENCIL;
      mask_export |= shader.output[i].frag_result == FRAG_RESULT_SAMPLE_MASK;
   }

   output.regs.db_shader_control =
      S_02880C_Z_ORDER(V_02880C_EARLY_Z_THEN_LATE_Z) |
      S_02880C_Z_EXPORT_ENABLE(depth_export) |
      S_02880C_STENCIL_REF_EXPORT_ENABLE(stencil_export) |
      S_02880C_MASK_EXPORT_ENABLE(mask_export) |
      S_02880C_KILL_ENABLE(shader.uses_kill);
   output.regs.spi_input_z =
      position_index >= 0 ? S_0286D8_PROVIDE_Z_TO_SPI(1) : 0;
}

static bool CopyProgram(const r600_shader &shader, void *&program, uint32_t &size)
{
   size = shader.bc.ndw * sizeof(uint32_t);
   program = align_malloc(size, 0x100);
   if (!program)
      return false;

   uint32_t *words = static_cast<uint32_t *>(program);
   for (unsigned i = 0; i < shader.bc.ndw; ++i)
      words[i] = util_cpu_to_le32(shader.bc.bytecode[i]);
   return true;
}

static void RemapLatteResources(r600_bytecode &bytecode)
{
   for (list_head *cf_node = bytecode.cf.next;
        cf_node != &bytecode.cf;
        cf_node = cf_node->next) {
      auto *control_flow = reinterpret_cast<r600_bytecode_cf *>(
         reinterpret_cast<char *>(cf_node) - offsetof(r600_bytecode_cf, list));
      for (list_head *tex_node = control_flow->tex.next;
           tex_node != &control_flow->tex;
           tex_node = tex_node->next) {
         auto *texture = reinterpret_cast<r600_bytecode_tex *>(
            reinterpret_cast<char *>(tex_node) - offsetof(r600_bytecode_tex, list));
         if (texture->resource_id >= R600_MAX_CONST_BUFFERS)
            texture->resource_id -= R600_MAX_CONST_BUFFERS;
      }

      for (list_head *vtx_node = control_flow->vtx.next;
           vtx_node != &control_flow->vtx;
           vtx_node = vtx_node->next) {
         auto *vertex_fetch = reinterpret_cast<r600_bytecode_vtx *>(
            reinterpret_cast<char *>(vtx_node) - offsetof(r600_bytecode_vtx, list));
         if (vertex_fetch->buffer_id < kMaxUniformBlocks)
            vertex_fetch->buffer_id += kLatteResourceBase;
      }
   }
}

static std::string DefaultUniformBlockWarning(mesa_shader_stage stage)
{
   const std::string setter = stage == MESA_SHADER_VERTEX ? "Vertex" : "Pixel";
   return std::string(
             "warning: shader mixes non-block uniforms with uniform blocks, which Latte "
             "cannot do in a single shader mode: GX2 selects one file or the other, and "
             "whichever one the mode leaves out reads back as zero. The non-block "
             "uniforms were moved into uniform block '") +
          kDefaultUniformBlockName + "' at location " +
          std::to_string(kDefaultUniformBlockBinding) + "; upload them with GX2Set" +
          setter + "UniformBlock instead of GX2Set" + setter + "UniformReg.";
}

static unsigned FragmentColorBufferCount(const nir_shader *nir)
{
   unsigned count = 0;
   for (unsigned i = 0; i < 8; ++i) {
      if (nir->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_DATA0 + i))
         count = i + 1;
   }
   return MAX2(count, 1u);
}

struct CafeCompiler::CompileState {
   gl_shader_program *shader_program = nullptr;
   gl_program *program = nullptr;
   nir_shader *nir = nullptr;
   mesa_shader_stage stage = MESA_SHADER_NONE;
   r600_pipe_shader_selector selector{};
   r600_pipe_shader pipe_shader{};
   r600_shader_key key{};
   std::vector<unsigned> ubo_bindings;
   bool uniform_registers = false;
   bool lower_loose_uniforms = false;
   std::array<unsigned, 32> vertex_semantics{};
   std::unordered_map<std::string, unsigned> sampler_bindings;
   ReflectionInfo reflection;

   ~CompileState()
   {
      if (list_is_linked(&pipe_shader.shader.bc.cf))
         r600_bytecode_clear(&pipe_shader.shader.bc);
      free(pipe_shader.shader.arrays);
      if (shader_program)
         standalone_destroy_shader_program(shader_program);
   }
};

CafeCompiler::CafeCompiler()
{
   m_valid = InitializeContext();
}

CafeCompiler::~CafeCompiler()
{
   if (m_isa)
      r600_isa_destroy(m_isa);
   free(m_rctx);
   free(m_screen);

   if (m_ctx) {
      free(m_ctx->screen);
      align_free(m_ctx);
   }
   delete m_nir_options;
   if (m_builtins_initialized)
      _mesa_glsl_builtin_functions_decref();
}

bool CafeCompiler::InitializeContext()
{
   m_ctx = static_cast<gl_context *>(align_calloc(sizeof(gl_context), 16));
   m_nir_options = new (std::nothrow) nir_shader_compiler_options;
   m_screen = static_cast<r600_screen *>(calloc(1, sizeof(r600_screen)));
   m_rctx = static_cast<r600_context *>(calloc(1, sizeof(r600_context)));
   m_isa = static_cast<r600_isa *>(calloc(1, sizeof(r600_isa)));
   if (!m_ctx || !m_nir_options || !m_screen || !m_rctx || !m_isa) {
      m_initialization_error = "Out of memory while initializing CafeGLSL";
      return false;
   }

   initialize_context_to_defaults(m_ctx, API_OPENGL_COMPAT);
   if (!m_ctx->screen) {
      m_initialization_error = "Out of memory while initializing the GLSL context";
      return false;
   }
   InitializeR600NirOptions(*m_nir_options);
   m_ctx->screen->nir_options[MESA_SHADER_VERTEX] = m_nir_options;
   m_ctx->screen->nir_options[MESA_SHADER_FRAGMENT] = m_nir_options;

   m_ctx->Version = 45;
   m_ctx->Extensions.Version = 45;
   m_ctx->Const.GLSLVersion = 450;
   m_ctx->Const.GLSLVersionCompat = 450;
   m_ctx->Const.AllowGLSLCompatShaders = true;
   /* agl builds GX2 shader source by stripping any #version and prepending
    * "#version 330" (aglShaderCompileInfo.cpp), so SDK-era shaders were written
    * expecting that and many omit the directive entirely - NSMBU has some that use
    * std140 blocks with no #version at all, which Mesa's 1.10 default has no such
    * thing as. Match what the hardware's front end saw. Unlike ForceGLSLVersion this
    * only fills in a version the shader did not state.
    */
   m_ctx->Const.DefaultGLSLVersion = 330;
   m_ctx->Const.NativeIntegers = true;
   m_ctx->Const.UniformBooleanTrue = ~0u;
   m_ctx->Const.GenerateTemporaryNames = true;
   m_ctx->Const.MaxClipPlanes = 8;
   m_ctx->Const.MaxDrawBuffers = 8;
   m_ctx->Const.MinProgramTexelOffset = -8;
   m_ctx->Const.MaxProgramTexelOffset = 7;
   m_ctx->Const.MaxLights = 8;
   m_ctx->Const.MaxTextureCoordUnits = 8;
   m_ctx->Const.MaxTextureUnits = 2;
   m_ctx->Const.MaxUniformBufferBindings = kMaxUniformBlocks;
   m_ctx->Const.MaxCombinedUniformBlocks = kMaxUniformBlocks * 2;
   m_ctx->Const.MaxUniformBlockSize = 64 * 1024;
   m_ctx->Const.MaxVertexStreams = 4;
   m_ctx->Const.MaxTransformFeedbackBuffers = 4;
   m_ctx->Const.MaxVarying = 15;
   m_ctx->Const.PointSizeFixed = true;
   m_ctx->Const.MaxUserAssignableUniformLocations =
      4 * MESA_SHADER_MESH_STAGES * MAX_UNIFORMS;

   for (mesa_shader_stage stage : {MESA_SHADER_VERTEX, MESA_SHADER_FRAGMENT}) {
      m_ctx->Const.Program[stage].MaxTextureImageUnits = kMaxSamplers;
      m_ctx->Const.Program[stage].MaxUniformBlocks = kMaxUniformBlocks;
      m_ctx->Const.Program[stage].MaxUniformComponents = 16 * 1024;
      m_ctx->Const.Program[stage].MaxCombinedUniformComponents =
         kMaxUniformBlocks * 16 * 1024;
   }
   m_ctx->Const.Program[MESA_SHADER_VERTEX].MaxAttribs = 32;
   m_ctx->Const.Program[MESA_SHADER_VERTEX].MaxOutputComponents = 128;
   m_ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxInputComponents = 128;
   m_ctx->Const.MaxCombinedTextureImageUnits = kMaxSamplers * 2;

   m_ctx->Extensions.ARB_ES3_compatibility = true;
   m_ctx->Extensions.ARB_ES3_1_compatibility = true;
   m_ctx->Extensions.ARB_ES3_2_compatibility = true;
   m_ctx->Extensions.ARB_explicit_uniform_location = true;
   m_ctx->Extensions.ARB_shading_language_packing = true;

   _mesa_glsl_builtin_functions_init_or_ref();
   m_builtins_initialized = true;

   m_screen->b.gfx_level = R700;
   m_screen->b.family = CHIP_RV730;
   m_rctx->screen = m_screen;
   m_rctx->b.gfx_level = R700;
   m_rctx->b.family = CHIP_RV730;
   m_rctx->isa = m_isa;
   if (r600_isa_init(R700, m_isa)) {
      m_initialization_error = "Failed to initialize the R700 ISA tables";
      return false;
   }

   return true;
}

bool CafeCompiler::Compile(const char *source,
                           unsigned shader_type,
                           CompileState &state,
                           std::string &diagnostics)
{
   if (!source) {
      diagnostics = "Shader source is null";
      return false;
   }

   const GLenum gl_shader_type = shader_type;
   state.stage = gl_shader_type == GL_VERTEX_SHADER ?
      MESA_SHADER_VERTEX : MESA_SHADER_FRAGMENT;
   state.shader_program = standalone_create_shader_program();
   state.shader_program->SeparateShader = true;

   gl_shader *shader = standalone_add_shader_source(
      m_ctx, state.shader_program, gl_shader_type, source);
   _mesa_glsl_compile_shader(m_ctx, shader, nullptr, false, false, true);
   if (shader->CompileStatus != COMPILE_SUCCESS) {
      diagnostics = shader->InfoLog ? shader->InfoLog : "GLSL compilation failed";
      return false;
   }

   state.shader_program->data->LinkStatus = LINKING_SUCCESS;
   link_shaders_init(m_ctx, state.shader_program);
   if (!gl_nir_link_glsl(m_ctx, state.shader_program) ||
       state.shader_program->data->LinkStatus != LINKING_SUCCESS) {
      diagnostics = state.shader_program->data->InfoLog ?
         state.shader_program->data->InfoLog : "GLSL linking failed";
      return false;
   }

   gl_linked_shader *linked = state.shader_program->_LinkedShaders[state.stage];
   if (!linked || !linked->Program || !linked->Program->nir) {
      diagnostics = "Mesa did not produce linked NIR";
      return false;
   }

   state.program = linked->Program;
   state.nir = state.program->nir;
   nir_shader_gather_info(state.nir, nir_shader_get_entrypoint(state.nir));
   nir_build_program_resource_list(&m_ctx->Const, state.shader_program, false);

   if (!PrepareNir(state, diagnostics))
      return false;
   if (!CompileR600(state, diagnostics))
      return false;

   return true;
}

bool CafeCompiler::PrepareNir(CompileState &state, std::string &diagnostics)
{
   if (state.stage == MESA_SHADER_VERTEX &&
       !BuildVertexSemanticMap(state.nir, state.vertex_semantics, diagnostics))
      return false;

   for (unsigned i = 0; i < state.program->info.num_ubos; ++i) {
      const gl_uniform_block *block = state.program->sh.UniformBlocks[i];
      if (block->Binding >= kMaxUniformBlocks) {
         diagnostics = "Uniform block binding exceeds Latte's 16 uniform-block slots";
         return false;
      }
      state.ubo_bindings.push_back(block->Binding);
   }

   nir_foreach_variable_with_modes(variable, state.nir, nir_var_mem_ubo) {
      if (!variable->data.explicit_binding) {
         diagnostics = "CafeGLSL currently requires explicit UBO bindings";
         return false;
      }
   }

   if (!AssignSamplerBindings(state.nir, state.sampler_bindings, diagnostics))
      return false;

   AssignUniformLocations(state.program, state.nir);

   /* GX2 selects one constant file per shader; the other reads back as zero. */
   const unsigned loose_components = state.program->Parameters ?
      state.program->Parameters->NumParameterValues : 0;
   state.lower_loose_uniforms = loose_components != 0 && !state.ubo_bindings.empty();
   state.uniform_registers = loose_components != 0 && !state.lower_loose_uniforms;

   if (state.uniform_registers && loose_components > kMaxAluConstComponents) {
      diagnostics = "Non-block uniforms exceed Latte's 1024-component ALU constant file";
      return false;
   }

   if (state.lower_loose_uniforms) {
      if (std::find(state.ubo_bindings.begin(),
                    state.ubo_bindings.end(),
                    kDefaultUniformBlockBinding) != state.ubo_bindings.end()) {
         diagnostics = "Uniform block binding 0 is reserved for non-block uniforms in "
                       "shaders that also use uniform blocks";
         return false;
      }
      diagnostics = DefaultUniformBlockWarning(state.stage);
   }

   if (!BuildReflection(state.shader_program,
                        state.program,
                        state.stage,
                        state.sampler_bindings,
                        state.lower_loose_uniforms,
                        state.reflection,
                        diagnostics))
      return false;

   NIR_PASS(_, state.nir, gl_nir_lower_buffers, state.shader_program);
   NIR_PASS(_, state.nir, nir_lower_system_values);

   state.nir->num_uniforms = state.program->Parameters ?
      DIV_ROUND_UP(state.program->Parameters->NumParameterValues, 4) : 0;
   NIR_PASS(_, state.nir, nir_lower_io, nir_var_uniform,
            UnpackedUniformSize, static_cast<nir_lower_io_options>(0));
   if (state.lower_loose_uniforms) {
      /* The pass shifts every other UBO index up by one. */
      NIR_PASS(_, state.nir, nir_lower_uniforms_to_ubo, false, false);
      state.ubo_bindings.insert(state.ubo_bindings.begin(),
                                kDefaultUniformBlockBinding);
   }
   NIR_PASS(_, state.nir, nir_opt_copy_prop);
   NIR_PASS(_, state.nir, nir_opt_constant_folding);
   if (!RemapUbos(state.nir, state.ubo_bindings, diagnostics))
      return false;

   NIR_PASS(_, state.nir, gl_nir_lower_samplers, nullptr);
   nir_shader_gather_info(state.nir, nir_shader_get_entrypoint(state.nir));
   r600_finalize_nir_common(state.nir, R700);
   return true;
}

bool CafeCompiler::CompileR600(CompileState &state, std::string &diagnostics)
{
   state.selector.nir = state.nir;
   state.selector.type = state.stage;
   state.selector.ir_type = PIPE_SHADER_IR_NIR;
   state.pipe_shader.selector = &state.selector;
   if (state.stage == MESA_SHADER_FRAGMENT)
      state.key.ps.nr_cbufs = FragmentColorBufferCount(state.nir);

   if (r600_shader_from_nir(m_rctx, &state.pipe_shader, &state.key)) {
      diagnostics = "The R600 SFN backend failed to compile the shader";
      return false;
   }

   const r600_shader &shader = state.pipe_shader.shader;
   if (state.stage == MESA_SHADER_FRAGMENT && shader.ninput > 32) {
      diagnostics = "Fragment shader exceeds Latte's 32 interpolator slots";
      return false;
   }
   if (shader.uses_images || shader.uses_atomics || shader.uses_tex_buffers ||
       shader.needs_scratch_space) {
      diagnostics = "Shader uses a resource class not yet supported by CafeGLSL";
      return false;
   }

   RemapLatteResources(state.pipe_shader.shader.bc);
   if (r600_bytecode_build(&state.pipe_shader.shader.bc)) {
      diagnostics = "Failed to encode R600 bytecode";
      return false;
   }
   return true;
}

GX2VertexShader *CafeCompiler::CompileVertexShader(const char *source,
                                                  std::string &diagnostics,
                                                  GLSL_COMPILER_FLAG flags)
{
   CompileState state;
   if (!Compile(source, GL_VERTEX_SHADER, state, diagnostics))
      return nullptr;

   std::unique_ptr<GX2VertexShader, decltype(&FreeVertexShader)> output(
      static_cast<GX2VertexShader *>(calloc(1, sizeof(GX2VertexShader))),
      FreeVertexShader);
   if (!output) {
      diagnostics = "Out of memory allocating GX2 vertex shader";
      return nullptr;
   }

   output->mode = state.uniform_registers ?
      GX2_SHADER_MODE_UNIFORM_REGISTER : GX2_SHADER_MODE_UNIFORM_BLOCK;
   if (!CopyProgram(state.pipe_shader.shader, output->program, output->size) ||
       !CopyReflection(state.reflection, output.get()) ||
       !CopyAttributes(state.reflection, output.get())) {
      diagnostics = "Out of memory copying vertex shader output";
      return nullptr;
   }
   FillVertexRegisters(state.pipe_shader, state.vertex_semantics, *output);

   if (flags & GLSL_COMPILER_FLAG_GENERATE_DISASSEMBLY)
      r600_bytecode_disasm(&state.pipe_shader.shader.bc);
   return output.release();
}

GX2PixelShader *CafeCompiler::CompilePixelShader(const char *source,
                                                std::string &diagnostics,
                                                GLSL_COMPILER_FLAG flags)
{
   CompileState state;
   if (!Compile(source, GL_FRAGMENT_SHADER, state, diagnostics))
      return nullptr;

   std::unique_ptr<GX2PixelShader, decltype(&FreePixelShader)> output(
      static_cast<GX2PixelShader *>(calloc(1, sizeof(GX2PixelShader))),
      FreePixelShader);
   if (!output) {
      diagnostics = "Out of memory allocating GX2 pixel shader";
      return nullptr;
   }

   output->mode = state.uniform_registers ?
      GX2_SHADER_MODE_UNIFORM_REGISTER : GX2_SHADER_MODE_UNIFORM_BLOCK;
   if (!CopyProgram(state.pipe_shader.shader, output->program, output->size) ||
       !CopyReflection(state.reflection, output.get())) {
      diagnostics = "Out of memory copying pixel shader output";
      return nullptr;
   }
   FillPixelRegisters(state.pipe_shader, *output);

   if (flags & GLSL_COMPILER_FLAG_GENERATE_DISASSEMBLY)
      r600_bytecode_disasm(&state.pipe_shader.shader.bc);
   return output.release();
}
