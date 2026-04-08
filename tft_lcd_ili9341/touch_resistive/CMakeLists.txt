add_library(touch_resistive
        touch_resistive.c
        )

target_include_directories(touch_resistive PUBLIC
        .
)

target_link_libraries(touch_resistive pico_stdlib ili9341 gfx hardware_adc)
