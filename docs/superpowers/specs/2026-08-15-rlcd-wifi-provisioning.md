# RLCD Wi-Fi Provisioning — Setup AP and Captive Portal

Date: 2026-08-15
Target: Waveshare ESP32-S3-RLCD-4.2
Status: Approved design, revised same-day for a WPA2-PSK setup AP and repaint-safe snapshot publishing (see Decision 1 and the Update boundary contract). Everything in this document is implemented in the working tree and verified on the host: `components/wifi_config` (credential validation, form parsing, WPA2 QR payload, passphrase formatter, state machine), `components/wifi_provision` (NVS store, `esp_wifi`/HTTP/DNS glue, `WIFI_AUTH_WPA2_PSK` setup AP with an `esp_fill_random` per-session passphrase), and the `components/ui` Setup page and repaint throttling.

Verified off-device only: 90 host test cases with 0 failures, a clean ESP-IDF build at 58% application-partition free, and the factory-backup size/hash gate. **No part of this has been flashed or confirmed on hardware.** Every row in the acceptance checklist below is still open.

## Goal

Let the board join the owner's Wi-Fi network without a phone app, a cloud backend, or typed credentials on a 400×300 monochrome panel. The board must go from "never configured" to "connected" using only its own display, its two buttons, and a phone's camera and browser.

## Non-goals

- SNTP / real clock sync over the network.
- Weather provider integration.
- Market/financial data provider integration.
- OTA firmware updates (the partition table stays single-factory-app; see Constraints).
- Any cloud or proxy service. The board is fully standalone; provisioning data never leaves the LAN formed by the board's own AP.

These stay out of this slice so provisioning correctness is not confused with later provider work. They are picked up in subsequent slices once this one is accepted.

## Approved decisions

### 1. Setup AP is WPA2-PSK, with the passphrase shown only on the board's screen

**Approved. This reverses the original "Setup AP is open (no password)" decision below (struck from effect, kept only as design history).**

The setup-mode AP uses `WIFI_AUTH_WPA2_PSK`. The passphrase is 8 characters drawn from an alphabet that excludes `0`, `O`, `1`, `l`, `I` (so a glance at the small on-screen text is never ambiguous), generated with `esp_random()` fresh on every entry into `SetupAp` (first boot or KEY long-press), and **never written to NVS** — it lives only in RAM for the duration of that setup session and is regenerated (not reused) the next time setup mode is entered. It is shown exclusively on the board's own screen; it is never printed, logged, or transmitted except as part of the QR payload the board itself renders. The QR carries `WIFI:T:WPA;S:RLCD-XXXXXX;P:<8 chars>;;`, so one scan both joins the AP and supplies the password — no separate typing step.

Rationale: the access gate moves from the application layer to the radio layer. An unauthorized device that has not seen the screen cannot associate to `RLCD-XXXXXX` at all, so it can never reach the HTTP server, the setup form, or the DNS responder — there is nothing at `192.168.4.1` for it to probe. No portal-level password form is added to compensate; none is needed, and none exists. This is a net removal of exposure, not an addition of a new auth code path: the change removes the "anyone in radio range can submit the form first" risk instead of gating it with more code. The new security model is simply "whoever can see the screen is authorized."

**Residual risk, stated plainly:** anyone who can physically see the board's screen during setup mode can read the QR or the printed passphrase and join. The passphrase stays visible for as long as setup mode is displayed (no blanking timer in this slice). This is materially smaller than the previous "anyone in radio range" exposure, but it is not zero — it is why setup mode still requires a deliberate KEY long-press to enter and why the board leaves `SetupAp` promptly on success, on the 5-retry fallback, or on a second long press.

**Considered and rejected: open AP + `http://192.168.4.1/?pw=xxx`.** An alternative that keeps the AP open but requires a password in the initial URL was considered and rejected. Reasons: a URL-embedded QR is useless before the phone has joined the network (the phone cannot resolve or route to `192.168.4.1` pre-join, so the QR would have to encode a two-step "join open AP, then open this URL" flow instead of the current one-step join); it turns provisioning into a two-step flow on every OS instead of the current one-scan join; the password ends up in the phone's browser history and possibly synced browsing data, which a WPA2 PSK never does; and it requires writing and maintaining an HTTP-level authentication form and its error states — exactly the code WPA2 makes unnecessary. WPA2-PSK dominates it on every axis.

### 2. Setup gesture is a KEY/GPIO18 long press (~2 s)

**Approved.** Holding KEY for approximately 2 seconds enters setup mode; the same gesture, while already in setup mode, exits it back to the normal carousel. KEY short release keeps its existing meaning, Previous page. BOOT/GPIO0 keeps short release = Next page and gains **no** long-press behavior of any kind.

Rationale: GPIO0 is the ROM boot strapping pin. Any application-level long-press hold on BOOT risks being confused with, or interfering with, the boot-time strap sampling window and the manual "hold BOOT, power on" downloader recovery path documented in `docs/hardware/first-layout-checklist.md` and the board skill. KEY/GPIO18 carries no strapping function, so it is the only button allowed to grow a new gesture. This is unchanged from the button ownership already recorded in the layout-carousel design doc.

This is implemented as `board::ButtonEvent::EnterSetup`, emitted by the existing debounced button poller on a KEY hold ≥ ~2000 ms, in addition to the existing `Next`/`Previous` short-release events. The 2 s long-press detector must not delay or suppress the short-release `Previous` event on releases under the threshold.

## Provisioning user journey

1. Board has no saved credentials (first boot) or the owner enters setup deliberately (KEY long press). Board generates a fresh 8-character passphrase via `esp_random()`, opens the WPA2-PSK `RLCD-XXXXXX` AP, and shows the Setup page with a QR code plus the passphrase as text.
2. Owner's phone scans the QR (`WIFI:T:WPA;S:RLCD-XXXXXX;P:<8 chars>;;`) and joins the AP in one step. No manual password typing (the QR carries it); the printed passphrase on-screen is a fallback for phones that can't scan.
3. Phone's OS captive-portal detection fires (see HTTP surface below) and offers/opens the settings page automatically, or the owner opens `http://192.168.4.1/` manually.
4. Owner enters the home network's SSID and password (or leaves password blank for an open home network) and submits the form.
5. Board validates the submission (`wifi_config::validate`), writes it to NVS, and attempts an STA connection.
6. On success: board leaves setup mode, tears down the AP/portal, and returns to the normal carousel. On failure: board stays in `SetupAp` state, keeps the portal reachable, and the Setup page shows a plain-language error so the owner can retry without re-entering setup mode.

No step requires a companion app, a cloud account, or a second device.

## NVS schema

Namespace: `wifi_cfg`, stored in the existing 24 KiB NVS partition at `0x9000` (`partitions.csv`, unchanged topology — no new partition is added for this slice).

| Key | Type | Meaning |
| --- | --- | --- |
| `ssid` | string, ≤ 32 bytes | STA SSID. Presence of this key is the "has saved credentials" signal. |
| `pass` | string, ≤ 63 bytes | STA password. Absent or empty means an open home network. |

These two keys are the owner's home-network credentials only. The setup AP's own WPA2 passphrase (Decision 1) is never stored here or anywhere in NVS — it is regenerated in RAM by `esp_random()` on every `SetupAp` entry and discarded when setup mode is left.

Clearing credentials (explicit path): erase both `ssid` and `pass` from the `wifi_cfg` namespace (`nvs_erase_key` per key, or `nvs_erase_all` scoped to the namespace), then commit. This is triggered by the same KEY long-press-to-enter-setup gesture followed by a successful new form submission (submitting new credentials overwrites, it does not require a separate "clear" action) — there is no destructive erase-only UI control in this slice. A factory-reset-style bulk erase is out of scope; the only clearing path is "enter setup, submit new form or wait for it to be resubmitted."

## Wi-Fi state machine

Implemented in `components/wifi_config::StateMachine` (host-tested, no ESP-IDF/LVGL dependency). States:

- **Connecting** — STA is attempting to associate using saved NVS credentials. Entry state at boot when `ssid` is present.
- **Connected** — STA has an IP. Normal operating state; carousel and future providers run.
- **SetupAp** — AP + captive portal + Setup page are active. Entry state at boot when no `ssid` is saved, or reached any time via the KEY long-press gesture.

Transitions:

- Boot, credentials present → `Connecting`.
- Boot, no credentials → `SetupAp`.
- `Connecting` + `on_connected()` → `Connected`.
- `Connecting` + `on_disconnected()`, retries < `kMaxRetries` (5) → `Connecting` (retry), retries incremented.
- `Connecting` + `on_disconnected()`, retries == `kMaxRetries` → `SetupAp` (fall back so the owner can fix a bad password without a manual gesture).
- Any state + `on_setup_gesture()` → `SetupAp` (from `Connected`, this drops the STA link deliberately) or, if already in `SetupAp`, back to `Connecting`/`Connected` depending on whether credentials exist — i.e. the gesture is a toggle out of setup, not just a way in.
- `SetupAp` + `on_credentials_saved()` → `Connecting`, retries reset to 0.
- `Connecting` + `on_connected()` from a retry always resets `retries()` to 0 on the next disconnect cycle (retry budget is per connection attempt, not cumulative across the device's uptime).

Retry bound: 5 attempts (`kMaxRetries`), matching the already-implemented `StateMachine`. No exponential backoff is specified in this slice; retry pacing is an implementation detail of the STA connect loop, not the state machine's concern.

## HTTP surface

Server: ESP-IDF `esp_http_server`, started only while `SetupAp` is active, bound to the AP's `192.168.4.1`. Modeled directly on `.tools/esp-idf/examples/protocols/http_server/captive_portal/`.

| Route | Method | Response |
| --- | --- | --- |
| `/` | GET | 200, `text/html`, the Setup page (QR + form). |
| `/` | POST | Parses `application/x-www-form-urlencoded` body via `wifi_config::parse_form`. Valid → save to NVS, `on_credentials_saved()`, 200 with a short "connecting…" HTML page. Invalid (`CredentialError != None`) → 200 (not a redirect) re-rendering the form with a plain-language inline error so the phone browser shows it without leaving the captive-portal webview. |
| any other path | GET/POST (404 handler) | `303 See Other`, `Location: /`, and a non-empty body (`httpd_resp_send`, not empty) — iOS's captive portal webview requires response content to recognize the redirect, an empty 30x is not sufficient. |

There is no third route and no auth-check code path anywhere on this server — access control is entirely the WPA2 join (Decision 1), not an HTTP-layer gate. Any device that can reach `192.168.4.1` at all is, by definition, a device that already had the passphrase.

Captive-portal probe URLs (must hit the wildcard 404 → redirect handler and get a response that each OS treats as "not the real internet", so the OS auto-opens the portal browser):

- Android: `http://connectivitycheck.gstatic.com/generate_204`, `http://www.google.com/generate_204`
- iOS/macOS: `http://captive.apple.com/hotspot-detect.html`

DNS: the AP runs the wildcard DNS responder from the reference `dns_server` component (`DNS_SERVER_CONFIG_SINGLE("*", "WIFI_AP_DEF")`), answering every A-record query with the AP's own IP so the above probe hostnames resolve to the board regardless of the phone's configured DNS.

## Setup LVGL page

Additive page only — this does not reopen the converged carousel layout. The Setup page is a new page kind shown only while `SetupAp` is active; it is not part of the automatic five-page mock-data carousel and does not compete with it for dwell time.

Content:

- Title: "Setup" plus the derived AP SSID (`RLCD-XXXXXX`) and the current 8-character passphrase as text, for the case the QR can't be scanned or the owner needs to type it into a phone that already joined once.
- QR code rendering the `WIFI:T:WPA;S:RLCD-XXXXXX;P:<8 chars>;;` payload (`wifi_config::wifi_qr_payload` extended to take the passphrase alongside the SSID), using LVGL's built-in `lv_qrcode` widget from `managed_components/lvgl__lvgl/src/libs/qrcode/`.
- One line of instructions: scan, join, then open the settings page (most phones open it automatically).
- Status line reflecting current sub-state: waiting for a phone to join / form submitted, connecting… / connected (about to leave setup) / error message from the last failed attempt (e.g. "wrong password" surfaced from a disconnect reason, or a generic "couldn't connect, try again" if the reason is unavailable).

**Build requirement:** `CONFIG_LV_USE_QRCODE` is currently unset in `sdkconfig.defaults` and must be added (`CONFIG_LV_USE_QRCODE=y`) as part of this slice's implementation, alongside the existing `CONFIG_LV_FONT_MONTSERRAT_*` lines. Without it, `lv_qrcode_create` does not exist and the Setup page cannot render its QR code.

Degraded/error states:

- QR code fails to allocate (LVGL/PSRAM pressure) → fall back to text-only SSID display; log the allocation failure. Do not block entry into `SetupAp` on QR success.
- No STA credentials saved and setup is entered for the first time → status line reads "not yet connected" (neutral, not an error).
- STA connect fails after 5 retries → status line shows a short reason (auth failure vs. AP-not-found vs. generic) and the page stays interactive so a new form submission can be retried without re-entering setup.
- Setup mode left idle indefinitely: no automatic timeout is specified in this slice — the owner exits via the same KEY long press. (An unattended timeout that blanks the on-screen passphrase is a reasonable future hardening item given the residual shoulder-surfing risk above, but is not required to accept this slice.)

## Update boundary contract (network ↔ LVGL)

Unchanged architecture boundary from the layout-carousel design: network/provisioning tasks (Wi-Fi event handlers, the HTTP server, the DNS responder) **never** call any `lv_*` API and never touch LVGL objects directly. They run in their own FreeRTOS tasks/callbacks and only publish state.

- Provisioning-relevant fields (current `StateMachine::State`, retry count, setup AP SSID, setup AP passphrase, last error reason) are copied into the existing immutable `AppSnapshot` the same way clock/market/weather data already is.
- An LVGL-thread timer (the same 100 ms timer already driving the carousel) reads the latest published snapshot and applies any Setup-page text/QR changes under the LVGL lock (`board::lvgl_lock()`/`unlock()`), exactly like existing page transitions.
- `components/wifi_config` and any Wi-Fi/HTTP/DNS glue stay outside `components/ui`; the UI only ever consumes `AppSnapshot` plus layout bounds, per the existing component boundary (`components/board_rlcd`, `components/app_core`, `components/ui`).

**Repaint throttling (standing constraint, shared with the system tray).** `ui::publish_snapshot()` is called on every Wi-Fi state transition (AP up, joined, retry, connected, disconnected) — potentially several times in quick succession during a retry storm, and independently every 30 s from the battery task described in `docs/superpowers/specs/2026-08-15-rlcd-system-tray-and-battery.md`. A full `render_page` rebuild on every publish would repaint the whole reflective panel that often, which is slow and visibly flashes on this display technology. The rule: the LVGL timer rebuilds the page only on a genuine page-identity change (entering/leaving Setup, or a carousel page change); for any other published change it updates only the specific labels whose text actually differs (Setup's SSID/passphrase/status text, the tray's network-status cell), following the existing `update_visible_clock` pattern rather than re-rendering. This is a general rule for all current and future snapshot publishers, not something specific to Wi-Fi.

## Failure and recovery behavior

| Scenario | Behavior |
| --- | --- |
| Wrong password submitted | STA disconnects repeatedly (auth failure reason); after 5 retries, `StateMachine` falls back to `SetupAp`; Setup page shows a wrong-password-shaped error; NVS still holds the bad credentials until the owner submits new ones. |
| Home AP out of range / unreachable | STA disconnects (no-AP-found reason); after 5 retries, falls back to `SetupAp` with a generic connect-failure message; same NVS/retry behavior as above. |
| Reboot with saved (good) credentials | Boots straight to `Connecting`, no AP/portal is started, carousel proceeds normally once connected — matches the "board must work standalone" constraint. |
| Reboot with no saved credentials | Boots straight to `SetupAp`; AP and portal start automatically without requiring the KEY gesture (first-run case), Setup page shows immediately. |
| KEY long press while `Connected` | Deliberately drops STA, enters `SetupAp`. Existing credentials in NVS are untouched unless a new form is submitted. |
| KEY long press while already in `SetupAp` | Toggles back out: attempts `Connecting` if credentials exist, otherwise has no better state to return to and stays in `SetupAp`. |

## Acceptance criteria

- [ ] `CONFIG_LV_USE_QRCODE=y` is set and the firmware builds with a rendering `lv_qrcode` on the Setup page.
- [ ] Setup AP `RLCD-XXXXXX` uses `WIFI_AUTH_WPA2_PSK`, SSID derived from the MAC via `wifi_config::setup_ap_ssid`, and an 8-character passphrase generated by `esp_random()` from the `0/O/1/l/I`-excluding alphabet on every entry into `SetupAp`.
- [ ] The generated passphrase is never written to NVS and is not reused across separate entries into setup mode (fresh regeneration each time).
- [ ] QR payload matches `wifi_config::wifi_qr_payload` output (`WIFI:T:WPA;S:RLCD-XXXXXX;P:<8 chars>;;`) and a phone camera scans it directly into a join prompt that completes with no manual password entry.
- [ ] The passphrase is also shown as on-screen text on the Setup page (fallback for a QR that can't be scanned).
- [ ] Android and iOS both auto-prompt to open the captive portal after joining the WPA2 AP (probe URLs above resolve and redirect through the board's DNS/HTTP handlers).
- [ ] A device that never saw the screen (does not have the passphrase) cannot associate to `RLCD-XXXXXX` and never reaches `192.168.4.1`.
- [ ] Submitting a valid SSID/password on `/` saves to NVS namespace `wifi_cfg` and the board reaches `Connected` (STA has an IP).
- [ ] Submitting an invalid form (per `wifi_config::validate`) re-shows the form with a readable inline error, no crash, no redirect loop.
- [ ] Wrong password: board retries up to 5 times, falls back to `SetupAp`, and the Setup page shows a readable error.
- [ ] AP out of range: same 5-retry fallback and readable error behavior as wrong password.
- [ ] Reboot with previously-good saved credentials reconnects with no AP/portal ever starting.
- [ ] Reboot with no saved credentials starts directly in `SetupAp` with no gesture required.
- [ ] KEY long press (~2 s) enters `SetupAp` from `Connected`, and the same gesture exits it back toward `Connecting`/`Connected` when credentials exist.
- [ ] Network/HTTP/DNS tasks make no direct `lv_*` calls; all Setup-page updates go through the existing `AppSnapshot` + LVGL-thread-timer path.
- [ ] A rapid sequence of Wi-Fi state publishes (e.g. a retry storm) does not cause a visible full-panel repaint per publish — only a genuine page-identity change triggers `render_page`; other Setup text/QR updates change only the differing labels.
- [ ] KEY/BOOT short-press navigation (Previous/Next) is re-verified as unaffected by the long-press addition, and GPIO0 ROM-downloader recovery (hold BOOT during power-on) is re-verified end to end after this slice, per `docs/hardware/first-layout-checklist.md`.
