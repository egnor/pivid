project('video_play', 'cpp', default_options: 'warning_level=3')

absl_dep = dependency('absl', method: 'cmake',
    modules: [
        'absl::flags',
        'absl::flags_parse',
        'absl::str_format',
    ]
)

libdrm_dep = dependency('libdrm')

libv4l2_dep = dependency('libv4l2')

executable('pivid_list', 'pivid_list.cpp', dependencies: [
    absl_dep,
    libdrm_dep,
    libv4l2_dep,
])
