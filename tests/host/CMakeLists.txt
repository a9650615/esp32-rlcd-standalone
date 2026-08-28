cmake_minimum_required(VERSION 3.16)
project(layout_carousel_host_tests LANGUAGES C CXX)

option(HOST_DEMO_MISSING_PAGE "Hide the deterministic weather page" OFF)

# The ESP-IDF lives once per workspace, in a versioned directory, and is
# resolved here exactly the way scripts/idf.sh resolves it - through the Git
# common directory, so a linked worktree finds the same toolchain its parent
# checkout does. Only cJSON is needed: the host tests parse the same JSON the
# firmware does, against the same copy of the parser.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../.idf-version" IDF_VERSION)
list(LENGTH IDF_VERSION IDF_VERSION_LINE_COUNT)
if(NOT IDF_VERSION_LINE_COUNT EQUAL 1 OR
   NOT IDF_VERSION MATCHES "^v[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR ".idf-version must contain one vMAJOR.MINOR.PATCH line")
endif()

get_filename_component(_project_dir "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
execute_process(
    COMMAND git -C "${_project_dir}" rev-parse --git-common-dir
    RESULT_VARIABLE _common_dir_result
    OUTPUT_VARIABLE _common_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_VARIABLE _common_dir_error)
if(NOT _common_dir_result EQUAL 0)
    message(FATAL_ERROR
        "cannot resolve Git common directory for ${_project_dir}: ${_common_dir_error}")
endif()
if(NOT IS_ABSOLUTE "${_common_dir}")
    set(_common_dir "${_project_dir}/${_common_dir}")
endif()
get_filename_component(_main_checkout_dir "${_common_dir}/.." ABSOLUTE)
get_filename_component(_workspace_dir "${_main_checkout_dir}/.." ABSOLUTE)
set(_candidate
    "${_workspace_dir}/.tools/esp-idf-${IDF_VERSION}/components/json/cJSON")
if(EXISTS "${_candidate}/cJSON.c")
    set(CJSON_DIR "${_candidate}")
else()
    message(FATAL_ERROR
        "cJSON for ${IDF_VERSION} not found at ${_candidate}; "
        "run ./scripts/bootstrap-idf.sh")
endif()


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(host_tests
    test_main.cpp
    test_page_registry.cpp
    test_media_registry.cpp
    test_now_playing.cpp
    test_carousel.cpp
    test_button_filter.cpp
    test_ui_theme.cpp
    test_ui_data.cpp
    test_tray_layout.cpp
    test_setup_page.cpp
    test_wifi_config.cpp
    test_battery.cpp
    test_net_time.cpp
    test_i2c_shtc3.cpp
    test_weather.cpp
    test_market.cpp
    test_net_log.cpp
    test_ota.cpp
    test_ota_quiesce.cpp
    test_settings_menu.cpp
    test_history.cpp
    test_audio_drain.cpp
    test_artwork.cpp
    ../../components/app_core/app_snapshot.cpp
    ../../components/app_core/history.cpp
    ../../components/app_core/page_registry.cpp
    ../../components/app_core/carousel_controller.cpp
    ../../components/app_core/tray_registry.cpp
    ../../components/app_core/media_registry.cpp
    ../../components/wifi_config/wifi_config.cpp
    ../../components/net_time/net_time_convert.cpp
    ../../components/weather/weather_parse.cpp
    ../../components/market/market_parse.cpp
    ../../components/market/market_schedule.cpp
    ../../components/net_log/net_log_ring.cpp
    ../../components/ota/ota_image.cpp
    ../../components/ota/ota_quiesce.cpp
    ../../components/ota/ota_prefix.cpp
    ../../components/ota/ota_version.cpp
    ../../components/ui/ui_strings.cpp
    ../../components/ui/settings_menu.cpp
    ../../modules/airplay/artwork.cpp
    ../../modules/airplay/tjpgd/tjpgd.c
    ${CJSON_DIR}/cJSON.c
)
target_include_directories(host_tests PRIVATE
    ../../components/app_core/include
    ../../components/board_rlcd/include
    ../../components/ui/include
    ../../components/wifi_config/include
    ../../components/net_time/include
    ../../components/weather/include
    ../../components/market/include
    ../../components/net_log/include
    ../../components/ota/include
    ../../modules/audio/include
    ../../modules/airplay
    ../../modules/airplay/tjpgd
    # ESP-IDF headers artwork.cpp includes unconditionally, stubbed for host.
    # See tests/host/esp_stubs/esp_heap_caps.h for why a shim and not an #ifdef.
    esp_stubs
    ${CJSON_DIR}
)
if(HOST_DEMO_MISSING_PAGE)
    target_compile_definitions(host_tests PRIVATE APP_CORE_DEMO_MISSING_PAGE=1)
endif()

enable_testing()
add_test(NAME host_tests COMMAND host_tests)

# Regression coverage for bind_i1_canvas()'s background-opacity parameter
# (components/ui/ui_theme.cpp). host_tests above deliberately never links
# real LVGL - ui_theme.hpp's UI_THEME_GEOMETRY_ONLY guard exists precisely so
# the rest of this suite can stay LVGL-free - but the bug this target checks
# for is that bind_i1_canvas() never forwards `background`/`background_opa`
# onto the canvas object's own style at all, which cannot be observed
# without a real lv_obj_t to read the style back from. Off by default so
# neither the shared `host_tests` baseline nor the other worktree's build
# picks up an LVGL build unasked; enable explicitly with
# -DUI_THEME_LVGL_TESTS=ON in a private build dir.
option(UI_THEME_LVGL_TESTS "Build the real-LVGL bind_i1_canvas regression test" OFF)
if(UI_THEME_LVGL_TESTS)
    set(LV_BUILD_CONF_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "" FORCE)
    set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
    add_subdirectory(../../managed_components/lvgl__lvgl lvgl_build)

    add_executable(ui_theme_lvgl_tests
        test_main.cpp
        test_ui_theme_lvgl.cpp
        ../../components/ui/ui_theme.cpp
        ../../components/ui/ui_fonts.cpp
        ../../components/ui/ui_strings.cpp
        ../../components/ui/settings_menu.cpp
        ../../components/ui/fonts/rlcd_cjk_14.c
        ../../components/ui/fonts/rlcd_cjk_20.c
        ../../components/ui/fonts/rlcd_cjk_28.c
        ../../components/ui/fonts/rlcd_digits_128.c
    )
    target_include_directories(ui_theme_lvgl_tests PRIVATE
        ../../components/app_core/include
        ../../components/board_rlcd/include
        ../../components/ui/include
        ../../components/wifi_config/include
    )
    # NDEBUG: ui_theme.cpp's only ESP-IDF-specific dependency (<esp_log.h>,
    # for a debug-only text-truncation warning) is compiled out by the same
    # #ifndef NDEBUG guard release firmware builds already use to drop it -
    # not a host-only carve-out, and irrelevant to the property under test.
    target_compile_definitions(ui_theme_lvgl_tests PRIVATE NDEBUG)
    target_link_libraries(ui_theme_lvgl_tests PRIVATE lvgl::lvgl)
    add_test(NAME ui_theme_lvgl_tests COMMAND ui_theme_lvgl_tests)
endif()
