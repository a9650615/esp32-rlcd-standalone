# Optional module (see Kconfig.projbuild, CONFIG_AIRPLAY_ENABLE): off means
# none of esp-raop-receiver, the vendored ALAC decoder, or airplay.cpp is
# compiled - no RAOP/mDNS/ALAC code ends up in the binary, and airplay.hpp's
# inline no-ops are everything a caller gets. PRIV_REQUIRES stays
# unconditional deliberately - see modules/audio/CMakeLists.txt's comment for
# why: ESP-IDF resolves a component's REQUIRES graph in an early pass that
# does not reliably see per-component Kconfig choices, so conditioning
# REQUIRES itself can silently drop entries from the build graph even though
# SRCS correctly follows the Kconfig value. Gating only SRCS here still
# removes every line of RAOP/ALAC code from the final link: nothing left in
# this component references those symbols, so the linker never pulls those
# archive members in, and the managed espressif/mdns dependency (see
# idf_component.yml) never gets compiled in either even though the component
# manager may still resolve/fetch it regardless of Kconfig.
set(airplay_srcs "")
if(CONFIG_AIRPLAY_ENABLE)
    list(APPEND airplay_srcs
        "airplay.cpp"
        # esp-raop-receiver (RAOP/AirPlay 1 protocol, RTP, RTSP, DMAP
        # metadata) - see UPSTREAM.md for provenance. lib/libalac.a and
        # main/ were not vendored; the RSA key was stripped out of raop.c
        # (see the comment above rsa_apply() there and README.md's "The RSA
        # key situation").
        "esp-raop-receiver/src/raop_core.c"
        "esp-raop-receiver/src/raop.c"
        "esp-raop-receiver/src/rtp.c"
        "esp-raop-receiver/src/util.c"
        "esp-raop-receiver/src/dmap_parser.c"
        "esp-raop-receiver/src/audio_buffer.c"
        # Apple's own open-source ALAC decoder (decode-only subset) plus
        # this repo's own thin C-API bridge onto it - replaces upstream's
        # prebuilt lib/libalac.a. See UPSTREAM.md.
        "alac/alac_wrapper.cpp"
        "alac/codec/ALACDecoder.cpp"
        "alac/codec/ALACBitUtilities.c"
        "alac/codec/EndianPortable.c"
        "alac/codec/ag_dec.c"
        "alac/codec/dp_dec.c"
        "alac/codec/matrix_dec.c"
    )
endif()

idf_component_register(
    SRCS ${airplay_srcs}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS
        "esp-raop-receiver/include"
        "esp-raop-receiver/private_include"
        "esp-raop-receiver/src"
        "alac/codec"
    PRIV_REQUIRES
        esp_netif
        mdns
        mbedtls
        nvs_flash
        esp_timer
        esp_hw_support
        lwip
        pthread
)

# --- The RSA key: builder-supplied, never in this repository ---
#
# raop.c no longer embeds Apple's AirPort Express RSA private key as a string
# literal (see the comment above rsa_apply() there for why: it is Apple's
# key, extracted from hardware, structurally required by AirPlay 1's
# handshake, and not this project's to distribute). Whoever enables
# CONFIG_AIRPLAY_ENABLE supplies their own copy at the path below, which
# modules/airplay/.gitignore keeps out of version control. Checked at CMake
# configure time, not left to surface as a mysterious missing-symbol link
# error only when the option happens to be on.
if(CONFIG_AIRPLAY_ENABLE)
    set(airplay_key_file "${CMAKE_CURRENT_SOURCE_DIR}/secrets/raop_private_key.pem")
    if(NOT EXISTS "${airplay_key_file}")
        message(FATAL_ERROR
            "CONFIG_AIRPLAY_ENABLE=y needs Apple's AirPort Express RSA "
            "private key, PEM-encoded, at:\n"
            "    ${airplay_key_file}\n"
            "This repository does not ship that file and will not: it is "
            "Apple's key, extracted from hardware, not this project's to "
            "distribute (see modules/airplay/README.md, \"The RSA key "
            "situation\"). Supply your own copy at the exact path above "
            "(modules/airplay/.gitignore keeps it out of git) and re-run "
            "the build, or set CONFIG_AIRPLAY_ENABLE=n.")
    endif()
    # EMBED_TXTFILES-equivalent: null-terminates the PEM (raop.c's
    # mbedtls_pk_parse_key call needs that terminator included in the length
    # it is given) and generates _binary_raop_private_key_pem_start/_end,
    # exactly the symbol names raop.c declares extern.
    target_add_binary_data(${COMPONENT_LIB} "${airplay_key_file}" TEXT)
endif()
