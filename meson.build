project('pivid', ['c', 'cpp'], version: '0.0',
    default_options: [
        'cpp_std=c++2a',
        'python.install_env=venv',
        'warning_level=3',
        'werror=true',
    ]
)

add_project_arguments(
    '-Wno-pedantic', '-Wno-psabi', '-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE',
    language: ['c', 'cpp']
)

util_deps = [
    dependency('doctest'),
    dependency('CLI11'),
    dependency('fmt'),
    dependency('spdlog'),
    meson.get_compiler('cpp').find_library('atomic'),
    meson.get_compiler('cpp').find_library('stdc++fs'),
]

libav_deps = [
    dependency('libavcodec'),
    dependency('libavfilter'),
    dependency('libavformat'),
    dependency('libavutil'),
]

linux_dep = dependency('linux-headers-generic')

python_bin = import('python').find_installation('python3')

display_mode_inc = custom_target(
    'display_mode.inc',
    output: 'display_mode.inc',
    input: 'display_mode_gen.py',
    command: [python_bin, '@INPUT@', '--output', '@OUTPUT@']
)

pivid_lib = library(
    'pivid', [
        'bezier_spline.cpp',
        'display_mode.cpp',
        display_mode_inc,
        'display_output.cpp',
        'frame_loader.cpp',
        'frame_player.cpp',
        'image_buffer.cpp',
        'interval.cpp',
        'media_decoder.cpp',
        'script_data.cpp',
        'script_runner.cpp',
        'unix_system.cpp',
    ],
    dependencies: [libav_deps, util_deps],
)

executable(
    'pivid_inspect_avformat', 'pivid_inspect_avformat.cpp',
    link_with: [pivid_lib],
    dependencies: [util_deps, libav_deps],
)

executable(
    'pivid_inspect_kms', 'pivid_inspect_kms.cpp',
    link_with: [pivid_lib],
    dependencies: [util_deps],
)

executable(
    'pivid_inspect_kmsg', 'pivid_inspect_kmsg.cpp',
    link_with: [pivid_lib],
    dependencies: [util_deps],
)

executable(
    'pivid_inspect_v4l2', 'pivid_inspect_v4l2.cpp',
    dependencies: [util_deps],
)

executable(
    'pivid_scan_media', 'pivid_scan_media.cpp',
    link_with: [pivid_lib],
    dependencies: [util_deps],
)

executable(
    'pivid_play', 'pivid_play.cpp',
    link_with: [pivid_lib],
    dependencies: [util_deps],
)

executable(
    'pivid_scan_displays', 'pivid_scan_displays.cpp',
    link_with: [pivid_lib],
    dependencies: [util_deps],
)

executable(
    'pivid_server', 'pivid_server.cpp',
    link_with: [pivid_lib],
    dependencies: [util_deps],
)

pivid_test = executable(
    'pivid_test', [
        'bezier_spline_test.cpp',
        'display_mode_test.cpp',
        'interval_test.cpp',
        'pivid_test_main.cpp',
        'script_data_test.cpp',
        'unix_system_test.cpp',
        'xy_test.cpp',
    ],
    link_with: [pivid_lib],
    dependencies: [util_deps],
)

test('pivid tests', pivid_test)
