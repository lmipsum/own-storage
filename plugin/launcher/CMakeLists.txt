# SPDX-License-Identifier: GPL-3.0-or-later
# The VSH plugin that starts Own Storage for the XMB's rows and shows its notifications; built with
# PS3DK:
#   cmake -S plugin/launcher -B build/launcher -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE="$PS3DK/cmake/ps3-ppu-toolchain.cmake"
#   cmake --build build/launcher
# which leaves owns_launcher.sprx in build/launcher.
cmake_minimum_required(VERSION 3.20)
project(owns-launcher C ASM)

include(ps3-self)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

ps3_prx_stub_library(paf_stub EXPORTS stubs/paf/exports.yml ARCHIVE_NAME paf)
ps3_prx_stub_library(vshtask_stub EXPORTS stubs/vshtask/exports.yml ARCHIVE_NAME vshtask)

add_executable(owns-launcher source/launcher.c)
target_compile_features(owns-launcher PRIVATE c_std_11)
target_compile_options(owns-launcher PRIVATE -Os -Wall -Wextra)
target_link_libraries(owns-launcher PRIVATE paf_stub vshtask_stub fs_stub lv2)

set(prx "${CMAKE_CURRENT_BINARY_DIR}/owns_launcher.prx")
set(read_only_prx "${CMAKE_CURRENT_BINARY_DIR}/owns_launcher.ro.prx")
set(sprx "${CMAKE_CURRENT_BINARY_DIR}/owns_launcher.sprx")

ps3_add_prx(owns-launcher NAME owns_launcher VERSION 1.0 OUTPUT_NAME owns_launcher.prx OUTPUT "${prx}")

# prx-gen's segment is writable and executable, which the PS3 does not load; it becomes read-only
# before make_sprx signs it.
add_custom_command(
    OUTPUT "${sprx}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/read_only_segment.py"
            "${prx}" "${read_only_prx}"
    COMMAND "${PS3_TOOL_make_sprx}" "${read_only_prx}" "${sprx}"
    DEPENDS owns-launcher "${CMAKE_CURRENT_SOURCE_DIR}/../tools/read_only_segment.py"
    VERBATIM)
add_custom_target(owns-launcher-sprx ALL DEPENDS "${sprx}")
