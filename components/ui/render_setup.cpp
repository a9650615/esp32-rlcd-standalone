#include "ui_app.hpp"
#include "ui_fonts.hpp"

namespace ui {
namespace {

const lv_font_t* medium_font() { return font_medium(); }
const lv_font_t* small_font() { return font_small(); }

}  // namespace

void render_setup(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                  Rect bounds, std::size_t page_index,
                  std::size_t page_count, UiContext* context) {
  (void)page_index;
  (void)page_count;
  apply_surface(parent);
  const SetupLayout layout = setup_layout(bounds);

  label(parent, text(Text::SetupTitle), layout.title, medium_font());
  const std::string ssid_text = setup_ssid_text(snapshot.setup.ap_ssid);
  label(parent, ssid_text.c_str(), layout.ssid, small_font());
  // Rendered larger than the surrounding rows and, below, allowed to wrap:
  // this is the one string on the page someone types into a browser by
  // hand, so it must never be silently clipped.
  const std::string password_text =
      setup_password_text(snapshot.setup.portal_password);
  lv_obj_t* password_label =
      label(parent, password_text.c_str(), layout.password, medium_font());
  if (password_label != nullptr) {
    lv_label_set_long_mode(password_label, LV_LABEL_LONG_WRAP);
  }
  label(parent, snapshot.setup.portal_url.c_str(), layout.portal, small_font());

  bool qr_ready = false;
#if LV_USE_QRCODE
  lv_obj_t* qr = lv_qrcode_create(parent);
  if (qr != nullptr) {
    lv_qrcode_set_size(qr, layout.qr.width);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    if (!snapshot.setup.qr_payload.empty() &&
        lv_qrcode_update(qr, snapshot.setup.qr_payload.c_str(),
                         static_cast<uint32_t>(
                             snapshot.setup.qr_payload.size())) ==
            LV_RESULT_OK) {
      lv_obj_set_pos(qr, layout.qr.x, layout.qr.y);
      qr_ready = true;
    } else {
      lv_obj_delete(qr);
    }
  }
#endif
  if (!qr_ready) {
    // Allocation failure or the widget being unavailable must never block
    // setup mode. Unlike the old WPA2-AP model, this is now a fully usable
    // fallback, not just an apology: the AP is open (no Wi-Fi password to
    // relay), and both portal_url and the page password are already on
    // screen for manual entry.
    lv_obj_t* fallback = label(parent, text(Text::SetupQrUnavailable),
                               layout.qr, small_font(), LV_TEXT_ALIGN_CENTER);
    // The QR rect is 200px square, so there is room to wrap; without this the
    // Chinese wording needs 207px on one line and loses its ending.
    if (fallback != nullptr) lv_label_set_long_mode(fallback, LV_LABEL_LONG_WRAP);
  }

  lv_obj_t* instructions = label(parent, text(Text::SetupInstructions),
                                 layout.instructions, small_font());
  if (instructions != nullptr) {
    lv_label_set_long_mode(instructions, LV_LABEL_LONG_WRAP);
  }
  const std::string status = setup_status_text(snapshot.setup.status);
  lv_obj_t* status_label = label(parent, status.c_str(), layout.status, small_font());
  if (status_label != nullptr) {
    // The status rect is sized for several wrapped lines (a failure message
    // plus a next-step hint) rather than the single short line a neutral
    // status needs - always allow wrapping so long text never gets clipped.
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_WRAP);
    apply_setup_status_style(status_label, snapshot.setup.error);
  }
  // Registered the same way render_tray registers its labels: render_page
  // swaps this into context->setup_status_label once the atomic replacement
  // completes, so ui_app.cpp's label-only repaint path can update just this
  // text on a status-only publish while Setup stays on screen.
  if (context != nullptr) context->staging_setup_status_label = status_label;
}

}  // namespace ui
