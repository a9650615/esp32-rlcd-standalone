/*----------------------------------------------*/
/* TJpgDec System Configurations R0.03          */
/*----------------------------------------------*/

/* This is a private configuration, not the one LVGL's bundled copy uses.
 *
 * LVGL vendors the same decoder at
 * managed_components/lvgl__lvgl/src/libs/tjpgd/, configured for its own image
 * decoder, with descaling switched off. Descaling is what this module needs and
 * cannot get there - it is a managed component whose contents are replaced on
 * update. That is the whole reason this copy exists; the output format is not,
 * since LVGL's deletions leave RGB888 as the only format this source can
 * actually produce (see JD_FORMAT below).
 *
 * See modules/airplay/UPSTREAM.md for provenance.
 */

#define JD_SZBUF        512
/* Stream input buffer. Left at the upstream default: the input comes from a
/  PSRAM buffer that already holds the whole image, so this only sets how much
/  is copied per read callback, and 512 bytes of that is not worth tuning. */

#define JD_FORMAT       0
/* Specifies output pixel format.
/  0: RGB888 (24-bit/pix)   <- this module
/  1: RGB565 (16-bit/pix)
/  2: Grayscale (8-bit/pix)
/
/  RGB888, not the grayscale this module actually wants, because THIS SOURCE
/  HAS NO GRAYSCALE PATH. LVGL deleted it: jd_mcu_output() in tjpgd.c guards
/  the whole pixel-building loop with `if (JD_FORMAT != 2)` and upstream's
/  matching `else` branch - the one that copies Y into workbuf - is not in the
/  file. Setting 2 therefore does not select grayscale output; it selects no
/  output at all, and outfunc is handed jd->workbuf still holding block_idct()
/  scratch. That decodes to something whose shapes are faintly recognisable,
/  because IDCT scratch correlates with the image, and whose tone is noise.
/  Measured, not reasoned: a 512x512 ramp came out as horizontal stripes.
/
/  So the luminance is computed in artwork.cpp's write_gray() instead, from
/  the BGR bytes this format does produce. One multiply-add per pixel, against
/  patching a vendored file - and this way the copy stays byte-identical to
/  what LVGL ships, so re-vendoring is a plain overwrite.
/
/  Do not "fix" this back to 2 without first checking that tjpgd.c has that
/  else branch. */

#define JD_USE_SCALE    1
/* Switches output descaling feature.
/  0: Disable
/  1: Enable   <- this module
/
/  This is the setting that makes cover art affordable here. jd_decomp() takes
/  a scale of 0..3 for 1/1, 1/2, 1/4, 1/8, applied during decode rather than
/  after, so a 600x600 artwork is never materialised at full size: at 1/4 it
/  comes out 150x150. LVGL's copy has this off, which is what makes its decoder
/  unusable for this without a full-size intermediate. */

#define JD_TBLCLIP      1
/* Use table conversion for saturation arithmetic. A bit faster, but increases 1 KB of code size.
/  0: Disable
/  1: Enable
*/

#define JD_FASTDECODE   1
/* Optimization level
/  0: Basic optimization. Suitable for 8/16-bit MCUs.
/  1: + 32-bit barrel shifter. Suitable for 32-bit MCUs.
/  2: + Table conversion for huffman decoding (wants 6 << HUFF_BIT bytes of RAM)
/
/  1, matching LVGL's copy. Level 2 buys speed for another 6 KB of work area,
/  and this decodes one image per track change - the extra memory is a worse
/  trade here than it would be in a viewer.
*/
