project(
  'gdeflate-benchmark',
  ['c', 'cpp'],
  license : 'MIT',
  default_options : ['c_std=c11', 'cpp_std=c++20']
)

prog_glslang = find_program('glslangValidator', native : true)

gdeflate_spv = custom_target(
    'gdeflate_shader.spv.h',
    input : 'third_party/gdeflate-shader/gdeflate.comp',
    output : 'gdeflate_shader.spv.h',
    command : [prog_glslang, '-V', '--target-env', 'spirv1.5', '-x', '-o', '@OUTPUT@', '@INPUT@'],
  )

deflate_inc = include_directories('third_party/libdeflate')
deflate_lib = static_library('deflate',
                             ['third_party/libdeflate/lib/gdeflate_compress.c',
                              'third_party/libdeflate/lib/gdeflate_decompress.c',
                              'third_party/libdeflate/lib/utils.c',
                              'third_party/libdeflate/lib/x86/cpu_features.c'],
                             include_directories: deflate_inc)

deflate_dep = declare_dependency(link_with: deflate_lib, include_directories: deflate_inc)

vulkan = dependency('vulkan')

executable('compress', 'compress.cpp', dependencies: [deflate_dep])
executable('decompress-cpu', 'decompress-cpu.cpp', dependencies: [deflate_dep])
executable('decompress-gpu', ['decompress-gpu.cpp', gdeflate_spv], dependencies: [vulkan])
