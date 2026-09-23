set(SRCS
    # --- 显示 / 输入 ---
    "lcd/sdgoods_lcd.c"
    "lcd/esp_lcd_st77916/esp_lcd_st77916.c"
    "src/sdgoods_lvgl.c"
    "src/sdgoods_input.c"
    "src/sdgoods_power.c"
    "src/sdgoods_boot_skip.c"

    # --- 板载外设与服务 ---
    "src/sdgoods_audio.c"
    "src/sdgoods_hw_info.c"
    "src/sdgoods_wifi.c"
    "src/sdgoods_ble.c"
    "src/sdgoods_jpeg.cpp"

    # --- BSP 基础能力 / 串口控制台（始终编译：'?' 能力查询应答方） ---
    "src/sdgoods_caps.c"
    "src/sdgoods_console.c"
    "src/sdgoods_nvs.c"      # NVS 保障：平台自己保证 NVS 可用，不假设 app 初始化过

    # --- 语言（默认英文，DEMO 页可切中文） ---
    "src/sdgoods_i18n.c"

    "src/sdgoods_hooks.c"

    # --- 应用框架与开机流程 ---
    "src/sdgoods_app_shell.c"
    "src/sdgoods_swipe_back.c"
    "src/sdgoods_swipe_up.c"
    "src/sdgoods_tap.c"      # 设备级手势策略：PRESS_LOCK 所有权 + 点按位移守卫
    "src/sdgoods_gesture.c"  # 应用级手势回调薄封装（组合复用上面三者，无新检测逻辑）

    # --- 中文字体子集（用 tools/gen_fonts.py 重新生成） ---
    "fonts/cn_font_14.c"
    "fonts/cn_font_16.c"
    "fonts/si_yuan_black_icon_14.c"
    "fonts/si_yuan_black_icon_16.c"
)

# --- 截屏能力（可选；由 Kconfig CONFIG_SDGOODS_SCREENSHOT 控制，默认开启） ---
if(CONFIG_SDGOODS_SCREENSHOT)
    list(APPEND SRCS "src/sdgoods_screenshot.c")
endif()

idf_component_register(
    SRCS ${SRCS}

    INCLUDE_DIRS
        "include"              # 应用层可见的全部平台接口
        "lcd"                  # st77916.h 依赖 esp_lcd_st77916.h，需一并公开
        "lcd/esp_lcd_st77916"

    PRIV_INCLUDE_DIRS
        "src"                  # 仅平台层内部

    REQUIRES
        lvgl
        bt
        app_update
        driver
        esp_adc
        jpegenc
        esp_event
        esp_hw_support
        esp_lcd
        esp_mm
        esp_netif
        esp_partition
        esp_psram
        bootloader_support
        esp_timer
        esp_wifi
        freertos
        hal
        heap
        log
        nvs_flash
        spi_flash
)
