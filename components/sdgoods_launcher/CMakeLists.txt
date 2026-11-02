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
# 驱动 tools/gen_app_info.py：读 <repo>/version.txt 并原样透传，生成
# generated/sdgoods_app_info.h（SDGOODS_APP_VERSION）。该头被 sdgoods_cc.c #include。
# 不再 +1：esp_app_desc.version 由 ESP-IDF 从同一份 version.txt 注入（弹框即读它），
# 两个显示必须同源，否则差 1。
#
# 🔴 2026-10-03 修（原实现是「always-run 的 add_custom_target（**无名输出**）+ 靠 #include
#    的 depfile 触发重编」，注释里断言"每次重建会改变 mtime ⇒ sdgoods_cc.c 随之重编"）。
#    **那个断言实测证伪** —— 症状是**「关于」页的版本号永远滞后一轮**：
#    v2.0.64 的镜像里 `esp_app_desc.version` 是 2.0.64，但代码段里硬留着 2.0.63
#    （`strings SDGOODS_LAUNCHER.bin | grep -E '^2\.0\.6[0-9]$'` 出两行就是它）。
#    机理：Ninja 在构建**开始时**读一次 mtime，那一刻 app_info.h 还是上一轮的
#      （实测 13:23:36 < sdgoods_cc.c.obj 的 13:23:37）⇒ 判 .o up-to-date 直接跳过；
#      随后那个 custom target 才把头上到 13:38:50。**最终状态是头比 .o 新、但这一轮没重编**，
#      于是下一次构建才补上 —— 正好差一轮。
#    根因：**无名输出的 custom target 在 Ninja 眼里没有生成规则** ⇒ 下游不知道它会被更新。
#    修法：改成**有 OUTPUT 的 add_custom_command**。Ninja 于是知道该文件由某条规则生成，
#      只要规则要跑（下面 DEPENDS 的 version.txt / 脚本变了），下游 .o 必然跟着重跑。
#    ⚠️ 别再改回 `add_custom_target(COMMAND …)`：那正是这个 bug 的形态。
#    ⚠️ 副作用（是好事）：version.txt 没变时不再无脑重写头文件 ⇒ sdgoods_cc.c 不必每轮重编。
set(_SDGOODS_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(_SDGOODS_GEN_HDR ${_SDGOODS_GEN_DIR}/sdgoods_app_info.h)
add_custom_command(
    OUTPUT  ${_SDGOODS_GEN_HDR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_SDGOODS_GEN_DIR}
    COMMAND python3 ${CMAKE_CURRENT_LIST_DIR}/tools/gen_app_info.py
            ${CMAKE_SOURCE_DIR} ${_SDGOODS_GEN_DIR}
    DEPENDS ${CMAKE_SOURCE_DIR}/version.txt
            ${CMAKE_CURRENT_LIST_DIR}/tools/gen_app_info.py
    COMMENT "sdgoods: SDGOODS_APP_VERSION <- version.txt"
    VERBATIM)
add_custom_target(sdgoods_gen_app_info ALL DEPENDS ${_SDGOODS_GEN_HDR})
add_dependencies(${COMPONENT_LIB} sdgoods_gen_app_info)
target_include_directories(${COMPONENT_LIB} PRIVATE ${_SDGOODS_GEN_DIR})
