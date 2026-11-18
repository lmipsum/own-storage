# SPDX-License-Identifier: GPL-3.0-or-later
# The VSH plugins: the launcher, which starts Own Storage for the XMB's rows, shows its
# notifications, registers the saves it downloaded, opens the save menu on Triangle and switches
# the XMB's list to the view chosen there, and the save menu's view with its RCO. Built with PS3DK:
#   cmake -S plugin -B build/plugin -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE="$PS3DK/cmake/ps3-ppu-toolchain.cmake" [-DOWNS_PLUGIN_LOG=ON]
#   cmake --build build/plugin
# which leaves owns_launcher.sprx, owns_menu.sprx and owns_menu.rco in build/plugin.
cmake_minimum_required(VERSION 3.20)
project(owns-plugins C ASM)

include(ps3-self)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

option(OWNS_PLUGIN_LOG "Log to /dev_hdd0/tmp/owns_menu.log, for a debug build" OFF)

ps3_prx_stub_library(paf_stub EXPORTS stubs/paf/exports.yml ARCHIVE_NAME paf)
ps3_prx_stub_library(vshtask_stub EXPORTS stubs/vshtask/exports.yml ARCHIVE_NAME vshtask)
ps3_prx_stub_library(allocator_stub EXPORTS stubs/allocator/exports.yml ARCHIVE_NAME allocator)
ps3_prx_stub_library(vshcommon_stub EXPORTS stubs/vshcommon/exports.yml ARCHIVE_NAME vshcommon)

set(tools "${CMAKE_CURRENT_SOURCE_DIR}/tools")

# A module of the plugin, signed as <name>.sprx. prx-gen's segment is writable and executable,
# which the PS3 does not load; it becomes read-only before make_sprx signs it.
function(owns_sprx target name)
    target_compile_features(${target} PRIVATE c_std_11)
    target_compile_options(${target} PRIVATE -Os -Wall -Wextra)
    target_include_directories(${target} PRIVATE common)
    target_sources(${target} PRIVATE common/owns_text.c)
    if(OWNS_PLUGIN_LOG)
        target_compile_definitions(${target} PRIVATE OWNS_PLUGIN_LOG=1)
        target_sources(${target} PRIVATE common/owns_log.c)
    endif()
    set(prx "${CMAKE_CURRENT_BINARY_DIR}/${name}.prx")
    set(read_only_prx "${CMAKE_CURRENT_BINARY_DIR}/${name}.ro.prx")
    set(sprx "${CMAKE_CURRENT_BINARY_DIR}/${name}.sprx")
    ps3_add_prx(${target} NAME ${name} VERSION 1.0 OUTPUT_NAME ${name}.prx OUTPUT "${prx}")
    add_custom_command(
        OUTPUT "${sprx}"
        COMMAND "${Python3_EXECUTABLE}" -I "${tools}/read_only_segment.py" "${prx}"
                "${read_only_prx}"
        COMMAND "${PS3_TOOL_make_sprx}" "${read_only_prx}" "${sprx}"
        DEPENDS ${target} "${tools}/read_only_segment.py"
        VERBATIM)
    add_custom_target(${target}-sprx ALL DEPENDS "${sprx}")
endfunction()

# What both modules share: the menu's target and rows, the view kept for the app, and the XMB's
# looks and input around the menu.
set(shared_sources
    common/menu_release.c
    common/menu_rows.c
    common/menu_target.c
    common/option_menu_close.c
    common/row_record.c
    common/utf8.c
    common/xmb_backdrop.c
    common/xmb_list.c
    common/xmb_state.c
    common/xmb_view.c)

add_executable(owns-launcher
    launcher/source/launcher.c
    launcher/source/app_start.c
    launcher/source/notices.c
    launcher/source/pad.c
    launcher/source/xmb_focus.c
    launcher/source/row_index.c
    launcher/source/item_menu.c
    launcher/source/menu_view.c
    launcher/source/option_menu.c
    launcher/source/menu_recovery.c
    launcher/source/explore_query.c
    launcher/source/list_files.c
    launcher/source/list_switch.c
    launcher/source/save_register.c
    ${shared_sources})
target_link_libraries(owns-launcher PRIVATE paf_stub vshtask_stub allocator_stub vshcommon_stub
                                            fs_stub lv2)
owns_sprx(owns-launcher owns_launcher)

add_executable(owns-menu
    menu/source/menu.c
    menu/source/submenu.c
    menu/source/row_labels.c
    menu/source/view_choice.c
    menu/source/save_menu.c
    menu/source/game_menu.c
    menu/source/info_page.c
    menu/source/confirm_page.c
    menu/source/app_action.c
    menu/source/menu_finish.c
    common/menu_text.c
    common/widget_text.c
    ${shared_sources})
target_link_libraries(owns-menu PRIVATE paf_stub allocator_stub vshcommon_stub fs_stub lv2)
owns_sprx(owns-menu owns_menu)

# The save menu's resource file: its pages (a copy of the system's option menu page, the
# information page, a copy of explore_plugin's, and the empty page a question's dialog opens on),
# and its texts.
set(rco "${CMAKE_CURRENT_BINARY_DIR}/owns_menu.rco")
add_custom_command(
    OUTPUT "${rco}"
    COMMAND "${Python3_EXECUTABLE}" -I "${tools}/rco_build.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/menu/owns_menu.json" "${rco}"
    DEPENDS "${tools}/rco_build.py" "${CMAKE_CURRENT_SOURCE_DIR}/menu/owns_menu.json"
    VERBATIM)
add_custom_target(owns-menu-rco ALL DEPENDS "${rco}")
