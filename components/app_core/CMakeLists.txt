idf_component_register(
    SRCS
        "app_snapshot.cpp"
        "history.cpp"
        "page_registry.cpp"
        "carousel_controller.cpp"
    INCLUDE_DIRS
        "include"
)

if(CONFIG_DEMO_MISSING_PAGE OR
   "${SDKCONFIG_DEFAULTS}" MATCHES "sdkconfig\.demo-missing-page")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_CORE_DEMO_MISSING_PAGE=1)
endif()
