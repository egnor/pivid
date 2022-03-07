project('pivid', ['c', 'cpp'], version: '0.0',
    default_options: [
        'cpp_std=c++2a',
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

pivid_lib = library(
    'pivid', [
        'cubic_bezier.cpp',
        'display_output.cpp',
        'frame_loader.cpp',
        'frame_loader.old.cpp',
        'frame_player.cpp',
        'image_buffer.cpp',
        'media_decoder.cpp',
        'thread_signal.cpp',
        'unix_system.cpp',
    ],
    dependencies: [libav_deps, util_deps],
)

executable(
    'pivid_inspect_avformat', 'pivid_inspect_avformat.cpp',
    dependencies: [util_deps, libav_deps],
)

executable(
    'pivid_inspect_kms', 'pivid_inspect_kms.cpp',
    link_with: [pivid_lib],
    dependencies: [util_deps],
)

executable(
    'pivid_inspect_v4l2', 'pivid_inspect_v4l2.cpp',
    dependencies: [util_deps],
)

executable(
    'pivid_save_frames', 'pivid_save_frames.cpp',
    link_with: [pivid_lib],
    dependencies: [util_deps],
)

executable(
    'pivid_play_media', 'pivid_play_media.cpp',
    link_with: [pivid_lib],
    dependencies: [util_deps],
)

pivid_test = executable(
    'pivid_test', [
        'cubic_bezier_test.cpp',
        'pivid_test_main.cpp',
        'xy_test.cpp',
    ],
    link_with: [pivid_lib],
    dependencies: [util_deps],
)

test('pivid tests', pivid_test)
