project('pivid', ['c', 'cpp'], version: '0.0',
    default_options: [
        'cpp_std=c++2a',
        'warning_level=3',
        'werror=true',
    ]
)

add_project_arguments('-Wno-pedantic', '-Wno-psabi', language: ['c', 'cpp'])

util_deps = [
    dependency('CLI11'),
    dependency('fmt'),
    meson.get_compiler('cpp').find_library('atomic'),
    meson.get_compiler('cpp').find_library('stdc++fs'),
]

libav_deps = [
    dependency('libavcodec'),
    dependency('libavfilter'),
    dependency('libavformat'),
    dependency('libavutil'),
]

libdrm_dep = dependency('libdrm')

linux_dep = dependency('linux-headers-generic')

pivid_lib = library(
    'pivid', [
        'media_decoder.cpp',
        'display_output.cpp',
        'unix_system.cpp',
    ],
    dependencies: [libav_deps, util_deps],
)

executable(
    'pivid_inspect_kms', 'pivid_inspect_kms.cpp',
    dependencies: [libdrm_dep, util_deps],
)

executable(
    'pivid_inspect_v4l2', 'pivid_inspect_v4l2.cpp',
    dependencies: [util_deps],
)

executable(
    'pivid_inspect_avformat', 'pivid_inspect_avformat.cpp',
    dependencies: [util_deps, libav_deps],
)

executable(
    'pivid_test_decode', 'pivid_test_decode.cpp',
    link_with: [pivid_lib],
    dependencies: [util_deps],
)

executable(
    'hello_drmprime', [
        'hello_drmprime/hello_drmprime.c',
        'hello_drmprime/drmprime_out.c',
    ],
    dependencies: [libav_deps, libdrm_dep],
)
