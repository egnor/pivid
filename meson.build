project('video_play', 'cpp', version: '0.0',
    default_options: [
        'warning_level=3',
        'cpp_std=c++17',
        'build.cpp_std=c++17',
        'werror=true',
    ]
)

cpp = meson.get_compiler('cpp')
libatomic_dep = cpp.find_library('atomic')
libstdc_fs_dep = cpp.find_library('stdc++fs')

fmt_dep = dependency('fmt')
gflags_dep = dependency('gflags')
libavcodec_dep = dependency('libavcodec')
libavformat_dep = dependency('libavformat')
libavutil_dep = dependency('libavutil')
libdrm_dep = dependency('libdrm')
libv4l2_dep = dependency('libv4l2')

executable(
    'pivid_inspect_kms', 'pivid_inspect_kms.cpp',
    dependencies: [fmt_dep, gflags_dep, libdrm_dep, libstdc_fs_dep]
)

executable(
    'pivid_inspect_v4l2', 'pivid_inspect_v4l2.cpp',
    dependencies: [fmt_dep, gflags_dep, libv4l2_dep, libstdc_fs_dep]
)

executable(
    'pivid_inspect_avformat', 'pivid_inspect_avformat.cpp',
    dependencies: [
        fmt_dep, gflags_dep, libstdc_fs_dep,
        libavcodec_dep, libavformat_dep, libavutil_dep, libatomic_dep,
    ]
)

executable(
    'pivid_test_decode', 'pivid_test_decode.cpp',
    dependencies: [
        fmt_dep, gflags_dep, libstdc_fs_dep,
        libavcodec_dep, libavformat_dep, libavutil_dep, libatomic_dep,
        libv4l2_dep
    ]
)
