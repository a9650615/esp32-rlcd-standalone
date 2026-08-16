cmake_minimum_required(VERSION 3.16)
project(layout_carousel_host_tests LANGUAGES C CXX)

option(HOST_DEMO_MISSING_PAGE "Hide the deterministic weather page" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(host_tests
    test_main.cpp
    test_page_registry.cpp
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
    test_settings_menu.cpp
    test_history.cpp
    ../../components/app_core/app_snapshot.cpp
    ../../components/app_core/history.cpp
    ../../components/app_core/page_registry.cpp
    ../../components/app_core/carousel_controller.cpp
    ../../components/app_core/tray_registry.cpp
    ../../components/wifi_config/wifi_config.cpp
    ../../components/net_time/net_time_convert.cpp
    ../../components/weather/weather_parse.cpp
    ../../components/market/market_parse.cpp
    ../../components/net_log/net_log_ring.cpp
    ../../components/ota/ota_image.cpp
    ../../components/ota/ota_prefix.cpp
    ../../components/ota/ota_version.cpp
    ../../components/ui/ui_strings.cpp
    ../../components/ui/settings_menu.cpp
    ../../.tools/esp-idf/components/json/cJSON/cJSON.c
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
    ../../.tools/esp-idf/components/json/cJSON
)
if(HOST_DEMO_MISSING_PAGE)
    target_compile_definitions(host_tests PRIVATE APP_CORE_DEMO_MISSING_PAGE=1)
endif()

enable_testing()
add_test(NAME host_tests COMMAND host_tests)
