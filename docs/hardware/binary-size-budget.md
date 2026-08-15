# Binary size budget

Date: 2026-08-16
Measured with `./scripts/idf.sh size-components` on commit-in-progress (all four
live providers wired, TLS enabled).

Application image `0x177d90` bytes against a `0x300000` factory partition —
**51% free**. There is no OTA slot, so the whole 3 MiB is available to one image
and nothing here is close to a limit. This record exists so a future decision
about what to cut starts from measurements rather than intuition.

## Where the flash actually goes

| Archive | Total | Flash code | Flash data | RAM (.bss) |
| --- | ---: | ---: | ---: | ---: |
| `liblvgl__lvgl.a` | 461,452 | 221,252 | 174,088 | **66,032** |
| `libnet80211.a` | 146,242 | 116,091 | 17,801 | 7,602 |
| `liblwip.a` | 110,645 | 102,477 | 4,018 | 4,134 |
| `libesp_app_format.a` | 101,975 | 435 | **101,530** | 10 |
| `libmbedtls.a` | 100,424 | 28,845 | 71,335 | 244 |
| `libmbedcrypto.a` | 78,883 | 71,483 | 6,980 | 252 |
| `libwpa_supplicant.a` | 65,262 | 62,348 | 1,576 | 1,330 |
| `libpp.a` | 64,158 | 41,912 | 3,905 | 1,234 |

Our own code, for contrast:

| Archive | Total |
| --- | ---: |
| `libui.a` | 20,749 |
| `libwifi_provision.a` | 10,913 |
| `libboard_rlcd.a` | 5,762 |
| `libapp_core.a` | 4,861 |

Everything this project wrote is roughly 42 KB, about 2% of the image. Nothing
we author is worth optimising for size; every large number below belongs to a
dependency or a configuration choice.

## The three worth knowing about

### LVGL — 461 KB flash, 66 KB RAM

The single largest contributor by a wide margin, and the 66 KB of `.bss` is the
part that matters more: that is internal RAM, which is far scarcer than flash on
this board.

Its 174 KB of flash data is dominated by the four compiled Montserrat faces
(`CONFIG_LV_FONT_MONTSERRAT_14/20/28/48` in `sdkconfig.defaults`). The 48 px
face exists solely for Home's Clock Hero. Dropping a face is the cheapest
size win available if one is ever needed, at the cost of a visibly coarser
type scale on a panel that has no colour to compensate with.

### Certificate bundle — ~101 KB of read-only data

`libesp_app_format.a` is normally negligible; the 101,530 bytes of flash data
attributed to it is the generated root-certificate bundle, pulled in by
`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y` in `sdkconfig.defaults`.
That setting compiles roughly 200 certificate authorities into the image.

This firmware talks to exactly four hosts: Open-Meteo, an IP-geolocation
service, TWSE, and Yahoo. `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_CMN`
(common CAs only) would cut this substantially.

It was deliberately left at full. The failure mode of a too-small bundle is a
provider that silently stops working months later when its issuer rotates to a
CA that was trimmed out — and this project has already lost several debugging
cycles to silent failures. 100 KB out of a 1.5 MB surplus is a cheap premium
against that. Revisit only if the budget actually tightens.

### TLS stack — ~194 KB combined

`libmbedtls` + `libmbedcrypto` + `libmbedx509` + `libesp-tls`. This is the price
of HTTPS and is not meaningfully reducible while the providers fetch over TLS.
Fetching over plain HTTP would be the only real saving, and is not worth
considering.

## What is not measured here

Task stacks are runtime RAM and do not appear above. The current allocations,
chosen in `main/app_main.cpp`:

| Task | Stack | Reason |
| --- | ---: | --- |
| `weather_monitor` | 16,384 | `weather::refresh()` puts an 8 KiB response buffer on the caller's stack; doubled for mbedTLS and cJSON depth |
| `market_monitor` | 8,192 | response body is heap-allocated, so only TLS and JSON depth need covering |
| `battery_monitor` | 3,072 | ADC only |
| `indoor_monitor` | 3,072 | I2C only |
| `net_time_monitor` | 3,072 | date arithmetic and `snprintf` only |

Main task stack is `CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192`, with 3,932 bytes
measured free after the first render. The project requires at least 2 KiB of
headroom there; that margin was already tight before any of this work and
should be re-measured after any change to the startup path.

## How to re-measure

```bash
./scripts/idf.sh size-components
```

Re-run this before concluding that anything needs trimming. The numbers above
are a snapshot, not a budget to defend.
