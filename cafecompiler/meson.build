cafecompiler_sources = files(
  'api.cpp',
  'cafe_compiler.cpp',
  'gfd_write.cpp',
)

libcafeglsl = static_library(
  'cafeglsl',
  cafecompiler_sources,
  include_directories : [
    inc_src,
    inc_include,
    inc_mesa,
    inc_compiler,
    inc_gallium,
    inc_gallium_aux,
    inc_gallium_drivers,
    inc_amd_common,
  ],
  link_whole : [libglsl_standalone, libr600, libgallium, _libgalliumvl_stub],
  dependencies : [idep_nir, idep_nir_headers, idep_mesautil],
  override_options : ['cpp_std=c++17'],
  gnu_symbol_visibility : 'hidden',
  install : true,
)

install_headers('CafeGLSLCompiler.h', 'cafe_gx2.h', 'gfd.h', subdir : 'cafeglsl')

# Version of the CafeGLSL API, as reported by GetGLSLCompilerVersion(). Separate from
# Mesa's own VERSION file.
cafeglsl_version = '0.3.0'

# Symbols the archive leaves undefined: the OS* mutex and thread calls need wut, the math
# functions need libm. stdc++ is only needed by consumers that link with gcc instead of g++.
cafeglsl_private_libs = ['-lwut', '-lm', '-lstdc++']

pkg.generate(
  name : 'cafeglsl',
  filebase : 'cafeglsl',
  description : 'GLSL to GX2 shader compiler for the Wii U (Latte) GPU, built from Mesa',
  version : cafeglsl_version,
  url : 'https://github.com/devkitPro/wut-packages',
  # Headers go to <includedir>/cafeglsl/, so consumers include <cafeglsl/CafeGLSLCompiler.h>
  # and need no extra -I beyond the portlibs include dir.
  libraries : ['-L${libdir}', '-lcafeglsl'],
  libraries_private : cafeglsl_private_libs,
)

_cafeglsl_cmake_conf = configuration_data()
_cafeglsl_cmake_conf.set('VERSION', cafeglsl_version)

configure_file(
  input : 'cafeglslConfig.cmake.in',
  output : 'cafeglslConfig.cmake',
  configuration : _cafeglsl_cmake_conf,
  install : true,
  install_dir : get_option('libdir') / 'cmake' / 'cafeglsl',
)

configure_file(
  input : 'cafeglslConfigVersion.cmake.in',
  output : 'cafeglslConfigVersion.cmake',
  configuration : _cafeglsl_cmake_conf,
  install : true,
  install_dir : get_option('libdir') / 'cmake' / 'cafeglsl',
)

if host_machine.system() != 'cafeos'
  cafeglsl_smoke = executable(
    'cafeglsl-smoke',
    'tests.cpp',
    include_directories : include_directories('.'),
    link_with : libcafeglsl,
    dependencies : [idep_mesautil],
    link_args : ld_args_gc_sections,
    override_options : ['cpp_std=c++17'],
  )

  test('cafeglsl-smoke', cafeglsl_smoke)

  cafeglsl_cli = executable(
    'glslcompiler.elf',
    ['main.cpp'],
    include_directories : include_directories('.'),
    link_with : libcafeglsl,
    dependencies : [idep_mesautil],
    link_args : ld_args_gc_sections,
    override_options : ['cpp_std=c++17'],
    install : true,
  )

  # use CLI to test a tricky shader
  test(
    'cafeglsl-skinning-regression',
    cafeglsl_cli,
    args : ['-vs'] + files('testdata/skinning-regression.vert'),
  )

  # use CLI to test various shader setups that NSMBU also uses
  test(
    'cafeglsl-uber-regression',
    cafeglsl_cli,
    args : ['-vs'] + files('testdata/nsmbu-uber.vert'),
  )
endif
