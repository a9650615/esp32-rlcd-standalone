idf_component_register(
    SRCS
        "net_log_ring.cpp"
        "net_log.cpp"
    INCLUDE_DIRS
        "include"
    PRIV_REQUIRES
        esp_netif
        # For the ESP_PM_NO_LIGHT_SLEEP lock held while an operator is
        # connected - see net_log.cpp. Private: nothing in net_log.hpp says so.
        esp_pm
)
