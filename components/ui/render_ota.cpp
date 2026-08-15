#include "ui_app.hpp"

namespace ui {
namespace {

const lv_font_t* phase_font() { return &lv_font_montserrat_28; }
const lv_font_t* percent_font() { return &lv_font_montserrat_48; }
const lv_font_t* small_font() { return &lv_font_montserrat_14; }

}  // namespace

// Deliberately plain: no tray, no page dots, no navigation affordance. While
// this page is up the firmware is writing its own flash, and every element
// that is not the state itself or the power warning is an invitation to do
// something during the one window where doing something is expensive.
void render_ota(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                Rect bounds, std::size_t page_index, std::size_t page_count,
                UiContext* context) {
  (void)page_index;
  (void)page_count;
  (void)context;
  apply_surface(parent);
  const OtaLayout layout = ota_layout(bounds);
  const app_core::OtaData& ota = snapshot.ota;

  label(parent, app_core::ota_phase_label(ota.phase), layout.phase,
        phase_font(), LV_TEXT_ALIGN_CENTER);

  // Percentage only when a feeder actually knew the total size. Everything
  // else on this panel already refuses to show a number it did not measure,
  // and a progress bar that invents its own fill is the same lie in a
  // friendlier shape.
  if (ota.percent_known) {
    char percent[8];
    std::snprintf(percent, sizeof(percent), "%u%%",
                  static_cast<unsigned>(ota.percent));
    label(parent, percent, layout.percent, percent_font(),
          LV_TEXT_ALIGN_CENTER);
  } else if (app_core::ota_owns_screen(ota)) {
    label(parent, kOtaProgressUnknown, layout.percent, percent_font(),
          LV_TEXT_ALIGN_CENTER);
  }

  // Only while flash is actually being written. On RolledBack or Failed the
  // write is over and telling someone not to power off would be false.
  if (app_core::ota_owns_screen(ota)) {
    label(parent, kOtaWarning, layout.warning, small_font(),
          LV_TEXT_ALIGN_CENTER);
  }

  if (!ota.detail.empty()) {
    lv_obj_t* detail =
        label(parent, ota.detail.c_str(), layout.detail, small_font(),
              LV_TEXT_ALIGN_CENTER);
    if (detail != nullptr) lv_label_set_long_mode(detail, LV_LABEL_LONG_WRAP);
  }
}

}  // namespace ui
