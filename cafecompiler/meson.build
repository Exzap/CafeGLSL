

dkp_root = '/opt/devkitpro'
wut_root = dkp_root + '/wut'

path_elf2rpl = dkp_root + '/tools/bin/elf2rpl'
path_rplimportgen = dkp_root + '/tools/bin/rplimportgen'
path_rplexportgen = dkp_root + '/tools/bin/rplexportgen'

files_cafecompiler = files(
'cafe_glsl_compiler.cpp', 
'cafe_glsl_compiler.h',
'api.cpp',
'tests.cpp',
'tests.h',
'libgfd/gfd_gx2.h',
'libgfd/gfd.h',
'libgfd/gfd_write.cpp',
'main.cpp',
)

cafeglslcompiler = executable('glslcompiler.elf', [files_cafecompiler],
  include_directories : [
    inc_src, inc_mapi, inc_mesa, inc_include, inc_compiler, inc_gallium, inc_gallium_aux, inc_amd_common,
  ],
  link_with : [libglsl, libgallium, libmesa, libglapi_static, libgalliumvl, libr600],
  dependencies: [dep_libdrm_radeon, dep_elf, idep_nir, idep_nir_headers, idep_mesautil, driver_r600],
  override_options: ['cpp_std=c++17'],
  gnu_symbol_visibility : 'hidden',
)

if host_machine.system() == 'cafeos'
custom_target('BuildRPX',
          depends : cafeglslcompiler,
          input : cafeglslcompiler,
          output : 'glslcompiler.rpx',
          command : [path_elf2rpl, '@INPUT@', '@OUTPUT@'],
          build_by_default : true)
      
# build RPL

# generate imports linker script
rplExportsOut = custom_target('RPLExports',
          input : 'exports.def',
          output : 'rpl_exports.s',
          command : [path_rplexportgen, '@INPUT@', '@OUTPUT@'],
          build_by_default : true)

# build RPL, it's almost the same as the standard executable except it also links in the generated rpl_export.s
cafeglslcompiler_rpl = executable('glslcompilerrplbase', [files_cafecompiler, rplExportsOut],
  include_directories : [
    inc_src, inc_mapi, inc_mesa, inc_include, inc_compiler, inc_gallium, inc_gallium_aux, inc_amd_common,
  ],
  link_with : [libglsl, libgallium, libmesa, libglapi_static, libgalliumvl, libr600],
  dependencies: [dep_libdrm_radeon, dep_elf, idep_nir, idep_nir_headers, idep_mesautil, driver_r600],
  override_options: ['cpp_std=c++17'],
  link_args: ['-specs=' + wut_root + '/share/rpl.specs'],
  gnu_symbol_visibility : 'hidden',
)

custom_target('BuildRPL',
          depends : cafeglslcompiler_rpl,
          input : cafeglslcompiler_rpl,
          output : 'glslcompiler.rpl',
          command : [path_elf2rpl, '@INPUT@', '--rpl', '@OUTPUT@'],
          build_by_default : true)
    
# build as static library
cafeglslcompiler_static = static_library('glslcompilerrplstatic', [files_cafecompiler],
  include_directories : [
    inc_src, inc_mapi, inc_mesa, inc_include, inc_compiler, inc_gallium, inc_gallium_aux, inc_amd_common,
  ],
  #link_whole : [libglsl, libgallium, libmesa, libglapi_static, libgalliumvl, libr600],
  link_with : [libglsl, libgallium, libmesa, libglapi_static, libgalliumvl, libr600],
  dependencies: [dep_libdrm_radeon, dep_elf, idep_nir, idep_nir_headers, idep_mesautil, driver_r600],
  override_options: ['cpp_std=c++17'],
  build_by_default: true,
  link_args :  ['--whole-archive', '--no-as-needed'],
  )
endif

# todo - run ELF2RPL
