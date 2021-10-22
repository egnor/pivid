project('video_play', 'cpp',
    default_options: [
        'warning_level=3',
        'cpp_std=c++17',
        'build.cpp_std=c++17',
    ]
)

cpp = meson.get_compiler('cpp')

fmt_dep = dependency('fmt')
gflags_dep = dependency('gflags')
libdrm_dep = dependency('libdrm')
libv4l2_dep = dependency('libv4l2')
stdc_fs_dep = cpp.find_library('stdc++fs')

executable('pivid_list', 'pivid_list.cpp', dependencies: [
    fmt_dep,
    gflags_dep,
    libdrm_dep,
    libv4l2_dep,
    stdc_fs_dep,
])
