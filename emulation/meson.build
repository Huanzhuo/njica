project('meica_vnf', 'cpp',
  version : '0.1',
  default_options : [
    'warning_level=2',
    'cpp_std=c++11',
    'buildtype=release',
    'werror=true',
  ]
)

host_system = host_machine.system()

cppc = meson.get_compiler('cpp')

cpp_flags = [
    '-Wlogical-op',
    '-Werror=missing-include-dirs',
    '-Werror=pointer-arith',
    '-Werror=init-self',
    '-Wfloat-equal',
    '-Wsuggest-attribute=noreturn',
    '-Werror=return-type',
    '-Werror=format=2',
    '-Wimplicit-fallthrough=5',
    '-Wendif-labels',
    '-Wstrict-aliasing=3',
    '-Wwrite-strings',
    '-Werror=overflow',
    '-Werror=shift-count-overflow',
    '-Werror=shift-overflow=2',
    '-Wdate-time',
    '-Wunused',
    '-Wduplicated-branches',
    '-Wmisleading-indentation',

    '-Wno-sign-compare',
    '-Wno-unused-parameter',

    '-ffast-math',
    '-fdiagnostics-show-option',
    '-fstack-protector',
    '-fstack-protector-strong',
    '--param=ssp-buffer-size=4',

    '-Wno-deprecated-declarations',
    '-Wno-unused-value',
    '-Wno-unused-variable',
    '-Wno-unused-but-set-variable',
]

add_project_arguments(
    cpp_flags,
    language: 'cpp',
)

ffpp_dep = dependency('libffpp', required: true)
boost_dep = dependency('boost')
boost_dep_modules = dependency('boost', modules : ['program_options'], required: true)

dep_list = [
  ffpp_dep,
  boost_dep,
  boost_dep_modules,
]

all_deps = declare_dependency(
  dependencies: dep_list,
)

# APPs
executable('meica_vnf',
           'meica_vnf.cpp','meica_vnf_utils.cpp',
           dependencies:all_deps,
           install : false)

executable('cnn_vnf',
           'cnn_vnf.cpp','meica_vnf_utils.cpp',
           dependencies:all_deps,
           install : false)

# Tests 
test_meica_vnf_utils = executable('test_meica_vnf_utils', 'test_meica_vnf_utils.cpp','meica_vnf_utils.cpp', dependencies:all_deps)
test('test_meica_vnf_utils', test_meica_vnf_utils)

# Linter
run_target('cppcheck', command: [
  'cppcheck','--enable=all',
  '--project=' + join_paths(meson.source_root(), 'build', 'compile_commands.json')
]) 

message('\n'.join([
  '',
  '        Host system:                  ' + host_system,
  '        Source code location:         ' + meson.source_root(),
  '        CPP compiler:                 ' + cppc.get_id(),
  '        Debugging support:            ' + get_option('buildtype'),
  '',
]))
