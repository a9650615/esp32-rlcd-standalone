#include "ui_app.hpp"
#include "ui_fonts.hpp"

#include <esp_app_desc.h>

#include <cstdio>

namespace ui {
namespace {

const lv_font_t* phase_font() { return font_large(); }

// The phase names live here rather than in app_core: that layer owns the state
// machine, not what the panel calls it, and translating the label is a UI
// concern. app_core::ota_phase_label stays as the log-facing wording.
Text ota_phase_text(app_core::OtaPhase phase) {
  switch (phase) {
    case app_core::OtaPhase::AwaitingConfirm:
      return Text::OtaAwaitingConfirm;
    case app_core::OtaPhase::Receiving:
      return Text::OtaUpdating;
    case app_core::OtaPhase::Writing:
      return Text::OtaFinishing;
    case app_core::OtaPhase::Verifying:
      return Text::OtaVerifying;
    case app_core::OtaPhase::RolledBack:
      return Text::OtaRolledBack;
    case app_core::OtaPhase::Failed:
      return Text::OtaFailed;
    case app_core::OtaPhase::Idle:
      break;
  }
  return Text::OtaWorking;
}
// Not font_hero(): that face carries ten digits and a colon, so "42%" loses
// its sign and "WORKING" is five empty boxes. The percentage is large but it
// is still text.
const lv_font_t* percent_font() { return font_large(); }
const lv_font_t* small_font() { return font_small(); }

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

  label(parent, text(ota_phase_text(ota.phase)), layout.phase,
        phase_font(), LV_TEXT_ALIGN_CENTER);

  // While the prompt is up the percentage row is free, and what belongs in it
  // is the thing the decision is actually about: which version replaces which.
  // "Something is being pushed from 192.168.3.111" is not enough to answer
  // with. The offered version comes out of the image's own descriptor, never
  // from whoever sent it - see OtaData::version.
  if (app_core::ota_awaits_confirm(ota)) {
    const esp_app_desc_t* running = esp_app_get_description();
    char line[48];
    if (!ota.version.empty() && running != nullptr) {
      // "->" rather than an arrow glyph: this face is Montserrat plus a CJK
      // fallback, and U+2192 is in neither, so an arrow would be a box on the
      // one screen that must be unambiguous.
      std::snprintf(line, sizeof(line), "%s -> %s", running->version,
                    ota.version.c_str());
    } else if (!ota.version.empty()) {
      std::snprintf(line, sizeof(line), "%s", ota.version.c_str());
    } else {
      line[0] = '\0';
    }
    if (line[0] != '\0') {
      label(parent, line, layout.percent, phase_font(), LV_TEXT_ALIGN_CENTER);
    }
  } else if (ota.percent_known) {
    char percent[8];
    std::snprintf(percent, sizeof(percent), "%u%%",
                  static_cast<unsigned>(ota.percent));
    label(parent, percent, layout.percent, percent_font(),
          LV_TEXT_ALIGN_CENTER);
  } else if (app_core::ota_owns_screen(ota)) {
    label(parent, text(Text::OtaWorking), layout.percent, percent_font(),
          LV_TEXT_ALIGN_CENTER);
  }

  // The prompt has to say which buttons answer it: this is the one screen
  // where they mean yes and no rather than navigation.
  if (app_core::ota_awaits_confirm(ota)) {
    label(parent, text(Text::OtaConfirmHint), layout.warning, small_font(),
          LV_TEXT_ALIGN_CENTER);
  }
  // Only while flash is actually being written. On RolledBack or Failed the
  // write is over and telling someone not to power off would be false.
  if (app_core::ota_owns_screen(ota)) {
    label(parent, text(Text::OtaDoNotPowerOff), layout.warning, small_font(),
          LV_TEXT_ALIGN_CENTER);
  }

  // During the write the version is what says the right thing is going on;
  // afterwards `detail` carries the failure reason and outranks it.
  if (!ota.detail.empty()) {
    label_wrapped(parent, ota.detail.c_str(), layout.detail, small_font(),
                  LV_TEXT_ALIGN_CENTER);
  } else if (!ota.version.empty() && app_core::ota_owns_screen(ota)) {
    label(parent, ota.version.c_str(), layout.detail, small_font(),
          LV_TEXT_ALIGN_CENTER);
  }
}

}  // namespace ui
