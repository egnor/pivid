project('video_play', 'cpp', default_options: 'warning_level=3')

absl_dep = dependency('absl', method: 'cmake',
    modules: [
        'absl::flags',
        'absl::flags_parse',
        'absl::str_format',
    ]
)

libdrm_dep = dependency('libdrm')

executable('pivid_list', 'pivid_list.cpp', dependencies: [
    absl_dep,
    libdrm_dep,
])
