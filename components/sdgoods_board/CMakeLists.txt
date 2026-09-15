idf_component_register(
    SRCS
        # --- 显示 / 输入 ---
        "lcd/st77916.c"
        "lcd/esp_lcd_st77916/esp_lcd_st77916.c"
        "src/lvgl_port.c"
        "src/touch_input.c"
        "src/power_off.c"

        # --- 板载外设与服务 ---
        "src/audio_recplay.c"
        "src/hw_info.c"
        "src/wifi_scan.c"
        "src/ble_scan.c"
        "src/screenshot.c"

        # --- 语言（默认英文，DEMO 页可切中文） ---
        "src/sdgoods_i18n.c"

        "src/sdgoods_hooks.c"

        # --- 应用框架与开机流程 ---
        "src/ui_app_shell.c"
        "src/ui_swipe_back.c"
        "src/ui_boot.c"
        "src/boot_anim_gif.c"

        # --- 中文字体子集（用 tools/gen_fonts.py 重新生成） ---
        "fonts/cn_font_14.c"
        "fonts/cn_font_16.c"
        "fonts/si_yuan_black_icon_14.c"
        "fonts/si_yuan_black_icon_16.c"

    INCLUDE_DIRS
        "include"              # 应用层可见的全部平台接口
        "lcd"                  # st77916.h 依赖 esp_lcd_st77916.h，需一并公开
        "lcd/esp_lcd_st77916"

    PRIV_INCLUDE_DIRS
        "src"                  # 仅平台层内部

    REQUIRES
        lvgl
        bt
        driver
        esp_adc
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
