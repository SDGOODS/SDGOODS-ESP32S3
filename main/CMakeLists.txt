# main/ —— 应用层（二次开发主要在这里）
#
# 平台层（屏 / 触摸 / 音频 / LVGL 移植 / 应用框架 / 字体）是独立组件：
#   components/sdgoods_board/
# 应用层通过 REQUIRES sdgoods_board 使用它，一行 #include "sdgoods_board.h" 即可。

idf_component_register(
    SRCS
        "main.c"

        # --- 应用层小工具（不注册成「应用」，只被 main.c / 页面调用）---
        # app_data_store：持久化数据的参考实现（appdata 优先，拿不到退 NVS）
        "app_data_store.c"

        # --- 应用注册表（新增应用要在这里接线） ---
        "apps/apps_registry.c"

        # --- 导航页 / 工具页 ---
        "apps/ui_home.c"
        "apps/ui_scan_page.c"
        "apps/ui_rec_page.c"
        "apps/ui_other_page.c"

        # --- 应用骨架（自己写的新应用加在这一段） ---
        # app_template.c 本身没有界面入口，它留在构建里的意义是「模板始终能编译」，
        # tools/new_app_project.py 派生新工程时会照抄它。删掉这个文件脚手架就废了。
        "apps/app_template.c"

        # --- 游戏 ---
        "apps/ui_flappy.c"

    INCLUDE_DIRS
        "apps"                  # 让 #include "ui_home.h" 直接可用
        "."                     # 让 #include "app_data_store.h" 直接可用

    REQUIRES
        sdgoods_board
        sdgoods_launcher
        bt
        driver
        esp_event
        esp_netif
        esp_timer
        esp_wifi
        freertos
        hal
        heap
        log
        lvgl
        nvs_flash
)

# ---- 自动编译版本号 (MMDDHHMM)，每次编译刷新 ----
# 生成 build_version.h 到组件构建目录，并让组件库在编译前依赖它，
# 同时把构建目录加入头文件搜索路径，使 apps/ui_home.c 能 #include "build_version.h"。
set(_bv_dir ${CMAKE_CURRENT_BINARY_DIR})
add_custom_target(gen_build_version ALL
    COMMAND ${CMAKE_COMMAND}
        -D BIN_DIR=${_bv_dir}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/gen_build_version.cmake
    BYPRODUCTS ${_bv_dir}/build_version.h
    COMMENT "Generating build version stamp (MMDDHHMM)"
)
add_dependencies(${COMPONENT_LIB} gen_build_version)
target_include_directories(${COMPONENT_LIB} PRIVATE ${_bv_dir})

# ---- 应用第三方组件补丁（LVGL 的 GIF canvas 走 PSRAM 等）----
# managed_components/ 不入库、由组件管理器下载，所以补丁放在 main/patches/ 随源码提交，
# 每次 configure 幂等地覆盖过去 —— 保证「clone 后直接编译」得到与发布版一致的固件。
# 详见 main/patches/README.md。该步失败会让 configure 直接报错（有意为之）。
idf_build_get_property(_patch_python PYTHON)
execute_process(
    COMMAND ${_patch_python} ${CMAKE_CURRENT_SOURCE_DIR}/patches/apply_lvgl_patches.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    RESULT_VARIABLE _lvgl_patch_rc
    OUTPUT_VARIABLE _lvgl_patch_out
    ERROR_VARIABLE  _lvgl_patch_err
)
if(_lvgl_patch_out)
    message(STATUS "${_lvgl_patch_out}")
endif()
if(NOT _lvgl_patch_rc EQUAL 0)
    message(FATAL_ERROR "LVGL 补丁应用失败: ${_lvgl_patch_err}")
endif()
