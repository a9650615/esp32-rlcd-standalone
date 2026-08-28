#include "card_contract.hpp"
#include "ui_app.hpp"
#include "ui_fonts.hpp"

namespace ui {

void render_card(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                 Rect bounds, std::size_t page_index, std::size_t page_count,
                 UiContext* context, uint8_t slot) {
  (void)snapshot;
  (void)context;
  // Page position lives in the system tray (render_tray in
  // render_shared.cpp), not a corner overlay on the page itself.
  (void)page_index;
  (void)page_count;
  (void)slot;
  apply_surface(parent);

  // ponytail: the contract and the page slot exist; the fetch that fills them
  // does not yet. Until it does this draws the same placeholder every other
  // page draws with no backing data, rather than nothing - a blank page and a
  // renderer that failed look identical on glass, and only one of them is
  // worth waking someone up for.
  //
  // What replaces this is one repack_i1_bits() into an I1 canvas: the mask
  // arrives already in the tight packing that function translates, and
  // card_body_bounds() is the box it goes in.
  label(parent, text(Text::NoData), no_data_rect(bounds), font_medium(),
        LV_TEXT_ALIGN_CENTER);
}

}  // namespace ui
