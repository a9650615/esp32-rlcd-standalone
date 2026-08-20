# Optional module (see Kconfig.projbuild, CONFIG_AUDIO_ENABLE): off means no
# audio.cpp is compiled - no ES8311/I2S code ends up in the binary, and
# audio.hpp's inline no-ops are everything a caller gets. Requires stays
# unconditional deliberately: ESP-IDF resolves a component's REQUIRES list in
# an early pass that runs before Kconfig choices are reliably visible to a
# per-component if(CONFIG_...), so conditioning REQUIRES itself silently
# dropped these (driver/esp_driver_gpio/esp_driver_i2s/board_rlcd) from the
# build graph even though SRCS correctly followed the Kconfig value - the
# managed espressif/esp_codec_dev dependency only kept working in that
# broken version because the component manager adds it via idf_component.yml,
# a separate mechanism from CMake's REQUIRES parsing. Gating only SRCS here
# still removes every line of codec/I2S code from the final link: nothing
# left in this component references the codec_dev or I2S symbols, so the
# linker never pulls those archive members in.
set(audio_srcs "")
if(CONFIG_AUDIO_ENABLE)
    list(APPEND audio_srcs "audio.cpp")
endif()

idf_component_register(
    SRCS ${audio_srcs}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        driver
        esp_driver_gpio
        esp_driver_i2s
        board_rlcd
        espressif__esp_codec_dev
        # For ota::register_quiesce_hook() (ota_quiesce.hpp) - audio has to
        # drop GPIO46 before a firmware write erases the slot, or the
        # amplifier sits enabled with an undriven input. Private: only
        # audio.cpp registers, audio.hpp's API says nothing about OTA.
        ota
        # For app_core::register_tray_indicator()/set_tray_indicator_active()
        # (tray_registry.hpp) - private, since only audio.cpp uses it, not
        # audio.hpp's public API. app_core has no dependents that would make
        # this circular, unlike the wifi_provision handler indirection an
        # earlier version of this module needed.
        app_core
)
