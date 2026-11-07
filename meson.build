project('libfprint-focaltech-c652', ['c', 'cpp'],
  version: '0.1.0',
  license: 'LGPL-2.1-or-later',
  default_options: ['warning_level=2', 'cpp_std=c++17', 'buildtype=release'])

tod    = dependency('libfprint-2-tod-1')
gusb   = dependency('gusb')
opencv = dependency('opencv4')
fprint = dependency('libfprint-2')
m      = meson.get_compiler('c').find_library('m')

tod_dir = tod.get_variable(pkgconfig: 'tod_driversdir')

ftmatch = static_library('ftmatch', 'src/ftmatch.cpp',
  dependencies: opencv, pic: true)

# The TOD plugin loaded by libfprint/fprintd
shared_module('fprint-2-tod-focaltech-c652', 'src/focaltech_c652.c',
  link_with: ftmatch,
  dependencies: [tod, gusb, opencv, m],
  gnu_symbol_visibility: 'hidden',
  c_args: ['-Wno-unused-parameter'],
  install: true,
  install_dir: tod_dir)

# Development tools (not installed)
executable('ftmatch_eval', 'src/ftmatch_eval.c', link_with: ftmatch, dependencies: [opencv, m])
executable('fptest', 'tools/fptest.c', dependencies: fprint)
