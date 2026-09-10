# Home clock digits: Nunito ExtraBold

- Source: https://github.com/google/fonts/tree/main/ofl/nunito
- Download: https://raw.githubusercontent.com/google/fonts/main/ofl/nunito/Nunito%5Bwght%5D.ttf
- Source SHA-256: `bb55a5ca5c2042335b3991af27c4d0705d0ef41cac6164ac737fd8f2a1e85207`
- License: SIL Open Font License 1.1; copyright and full terms in `Nunito-OFL.txt`.
- Instance: upright Nunito, `wght=800` (ExtraBold), generated with fontTools 4.65.0.
- Converter: LVGL `lv_font_conv` 1.5.3, 108 px, 4 bpp, digits U+0030–U+0039 only.
- The 108 px em yields a 79 px digit line height (baseline 2); native glyphs are 77–79 px tall.
- No kerning; all digits have the same advance (1037/16 px). Labels use zero letter spacing.
- Uncompressed because the existing project disables `LV_USE_FONT_COMPRESSED`.
  Digit bitmap storage is 22,217 bytes, excluding small descriptors. No TTF/OTF is shipped.
- Resource name: `s1_nunito_extrabold_108`.

Reproduce in a temporary directory (raw font files must stay outside the repository):

```sh
python -m fontTools.varLib.instancer Nunito.ttf wght=800 --output Nunito-ExtraBold.ttf
lv_font_conv --size 108 --bpp 4 --format lvgl --font Nunito-ExtraBold.ttf \
  --symbols 0123456789 --no-kerning --no-compress \
  --lv-font-name s1_nunito_extrabold_108 --lv-include lvgl.h \
  --output s1_nunito_extrabold_108.c
```

Home uses two 170 px labels at x=0 with `LV_TEXT_ALIGN_CENTER`, without transforms
or optical x offsets. Existing y positions (101 and 204), colors, weather, date
and small clock remain intact. Legacy hidden Home font remains for existing UI code.

Validation: build the hardware-free target with `S1_UI_TESTS_ONLY=ON`, then run
CTest. The test renders Home and checks both labels for every value 00–99:
full-screen horizontal bounds, center alignment, unit scale, no wrapping and
vertical clearance before the date. HA functions are stubs in this target.
