file(GLOB_RECURSE UI_SRCS "ui/*.c")

idf_component_register(
    SRCS
        "main.c"
        "../components/waveshare_rgb_lcd_port.c"
        ${UI_SRCS}
    INCLUDE_DIRS
        "."
        "ui"
        "../components"
        "${CMAKE_CURRENT_LIST_DIR}/../managed_components/lvgl__lvgl/include"
    REQUIRES
        lvgl
        esp_lvgl_adapter
        esp_lcd_touch_gt911
)
