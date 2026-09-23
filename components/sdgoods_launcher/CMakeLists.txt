idf_component_register(
    SRCS
        "src/slot_manifest.c"
        "src/slot_cover.c"       # 槽尾保留区里的主页封面图（见 sdgoods_slot_cover.h 的格式表）
        "src/app_sdk.c"
        "src/launcher.c"
        "src/device_mode.c"
        "src/sdgoods_cc.c"       # 控制中心：设备级统一系统浮层（启动器与所有 app 共用）
    INCLUDE_DIRS
        "include"
    REQUIRES
        app_update
        sdgoods_board
        driver
        esp_app_format
        esp_partition
        esp_system
        fatfs
        wear_levelling
        mbedtls
        nvs_flash
        log
        freertos
        lvgl

    # ⚠️ 必须 WHOLE_ARCHIVE：控制中心（sdgoods_cc.c）要成为**每个 app 的默认能力**，
    # 而 BSP 侧只能通过弱符号 sdgoods_cc_open 去调它（sdgoods_launcher 依赖 sdgoods_board，
    # 反向直接依赖会成环）。C 静态库只按「未定义符号」拉取成员对象 —— 若不整档链接，
    # 一旦没有别的代码引用 sdgoods_cc_* ，sdgoods_cc.o 就不会进链接，
    # 弱默认空实现被当成最终实现 ⇒ **app 里控制中心静默失效**（最难查的一类问题）。
    # 整档链接后 sdgoods_cc.o 必然在场，其强符号覆盖 BSP 的弱默认。
    WHOLE_ARCHIVE
)

# ---- 构建信息生成（SDGOODS_APP_VERSION 透传 version.txt，不再末位 +1）----
# always-run 的 custom target 驱动 tools/gen_app_info.py：读 <repo>/version.txt 并原样透传，
# 生成 generated/sdgoods_app_info.h（SDGOODS_APP_VERSION）。该头被 sdgoods_cc.c #include，
# 每次重建会改变其 mtime ⇒ sdgoods_cc.c 随之重编。
# 不再 +1：esp_app_desc.version 由 ESP-IDF 从同一份 version.txt 注入（弹框长按菜单读它），两个显示必须同源，否则差 1。
# （不声明为 OBJECT_DEPENDS：Ninja 要求每个输入都有生成规则，无名输出的 custom target 不适合那样写；
#  靠 #include 的依赖文件 + always-run target 已能保证重编。）
set(_SDGOODS_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
add_custom_target(sdgoods_gen_app_info ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_SDGOODS_GEN_DIR}
    COMMAND python3 ${CMAKE_CURRENT_LIST_DIR}/tools/gen_app_info.py
            ${CMAKE_SOURCE_DIR} ${_SDGOODS_GEN_DIR}
    VERBATIM)
add_dependencies(${COMPONENT_LIB} sdgoods_gen_app_info)
target_include_directories(${COMPONENT_LIB} PRIVATE ${_SDGOODS_GEN_DIR})
