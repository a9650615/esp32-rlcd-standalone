cmake_minimum_required(VERSION 3.16)
project(layout_carousel_host_tests LANGUAGES CXX)

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
    ../../components/app_core/app_snapshot.cpp
    ../../components/app_core/page_registry.cpp
    ../../components/app_core/carousel_controller.cpp
)
target_include_directories(host_tests PRIVATE
    ../../components/app_core/include
    ../../components/board_rlcd/include
    ../../components/ui/include
)
if(HOST_DEMO_MISSING_PAGE)
    target_compile_definitions(host_tests PRIVATE APP_CORE_DEMO_MISSING_PAGE=1)
endif()

enable_testing()
add_test(NAME host_tests COMMAND host_tests)
