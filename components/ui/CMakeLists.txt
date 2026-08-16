idf_component_register(
    SRCS
        "ui_theme.cpp"
        "ui_app.cpp"
        "render_shared.cpp"
        "render_home.cpp"
        "render_market.cpp"
        "render_weather.cpp"
        "render_indoor.cpp"
        "render_setup.cpp"
        "render_ota.cpp"
        "render_settings.cpp"
        "ui_strings.cpp"
        "settings_menu.cpp"
        "ui_fonts.cpp"
        "ui_screenshot.cpp"
        "fonts/rlcd_cjk_14.c"
        "fonts/rlcd_cjk_20.c"
        "fonts/rlcd_cjk_28.c"
        "fonts/rlcd_digits_128.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        app_core
        esp_app_format
        board_rlcd
        lvgl__lvgl
        mbedtls
)

# Every build, not every configure: ui_strings.cpp changes far more often than
# this file does, and a coverage gap between the strings and the generated font
# is invisible at runtime - LVGL draws a missing glyph as an empty box and says
# nothing. Adding strings without regenerating the font shipped 32 characters
# of tofu once already.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_custom_target(check_cjk_font ALL
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_LIST_DIR}/../../scripts/check-cjk-font.py
        DEPENDS ${CMAKE_CURRENT_LIST_DIR}/ui_strings.cpp
        COMMENT "Checking CJK glyph coverage against ui_strings.cpp"
        VERBATIM)
    add_dependencies(${COMPONENT_LIB} check_cjk_font)
endif()
