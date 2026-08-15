idf_component_register(
    SRCS "app_main.cpp"
    PRIV_REQUIRES app_core board_rlcd ui driver wifi_provision net_time weather market
)
