project('video_play', 'cpp', version: '0.0',
    default_options: [
        'warning_level=3',
        'cpp_std=c++17',
        'build.cpp_std=c++17',
        'werror=true',
    ]
)

cpp = meson.get_compiler('cpp')
add_project_arguments(['-Wno-non-virtual-dtor'], language: 'cpp')

util_deps = [
    dependency('CLI11'),
    dependency('fmt'),
    cpp.find_library('atomic'),
    cpp.find_library('stdc++fs'),
]

libav_deps = [
    dependency('libavcodec'),
    dependency('libavformat'),
    dependency('libavutil'),
]

libdrm_dep = dependency('libdrm')

executable(
    'pivid_inspect_kms', 'pivid_inspect_kms.cpp',
    dependencies: [util_deps, libdrm_dep],
)

executable(
    'pivid_inspect_v4l2', 'pivid_inspect_v4l2.cpp',
    dependencies: [util_deps]
)

executable(
    'pivid_inspect_avformat', 'pivid_inspect_avformat.cpp',
    dependencies: [util_deps, libav_deps],
)

executable(
    'pivid_test_decode', 'pivid_test_decode.cpp',
    dependencies: [util_deps, libav_deps],
)
