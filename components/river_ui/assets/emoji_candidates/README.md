# Noto Cat Emoji Animation Set

This folder holds the selected Noto animated cat emoji GIF set for the Orvibo
LVGL emoji display follow-up. The previous Pixabay GIF candidates were removed
after selecting the Noto style.

These files are raw candidate assets only; they are not yet converted into LVGL
image descriptors or selected for firmware playback.

## Downloaded Assets

| File | Codepoint | Source | License noted by source | Size / frames |
| --- | --- | --- | --- | --- |
| `noto_cat_face_1f431.gif` | `1f431` | Google Noto animated emoji | CC BY 4.0 | 512x512, 75 frames, 655527 bytes |
| `noto_smile_cat_1f638.gif` | `1f638` | AnimatEmojis / Google Noto animated emoji | CC BY 4.0 | 512x512, 96 frames, 931511 bytes |
| `noto_joy_cat_1f639.gif` | `1f639` | AnimatEmojis / Google Noto animated emoji | CC BY 4.0 | 512x512, 88 frames, 1076135 bytes |
| `noto_smiley_cat_1f63a.gif` | `1f63a` | AnimatEmojis / Google Noto animated emoji | CC BY 4.0 | 512x512, 61 frames, 516429 bytes |
| `noto_heart_eyes_cat_1f63b.gif` | `1f63b` | AnimatEmojis / Google Noto animated emoji | CC BY 4.0 | 512x512, 77 frames, 880540 bytes |
| `noto_smirk_cat_1f63c.gif` | `1f63c` | AnimatEmojis / Google Noto animated emoji | CC BY 4.0 | 512x512, 53 frames, 435526 bytes |
| `noto_kissing_cat_1f63d.gif` | `1f63d` | AnimatEmojis / Google Noto animated emoji | CC BY 4.0 | 512x512, 63 frames, 771899 bytes |
| `noto_pouting_cat_1f63e.gif` | `1f63e` | Google Noto animated emoji | CC BY 4.0 | 512x512, 57 frames, 519944 bytes |
| `noto_crying_cat_1f63f.gif` | `1f63f` | AnimatEmojis / Google Noto animated emoji | CC BY 4.0 | 512x512, 51 frames, 465013 bytes |
| `noto_scream_cat_1f640.gif` | `1f640` | AnimatEmojis / Google Noto animated emoji | CC BY 4.0 | 512x512, 55 frames, 658461 bytes |
| `preview_contact_sheet.png` | local | Local preview sheet | Derived from downloaded candidates | 600x750 PNG |

## Source URLs

- `https://animatemojis.com/emoji/smiley-cat`
- `https://animatemojis.com/emoji/smile-cat`
- `https://animatemojis.com/emoji/joy-cat`
- `https://animatemojis.com/emoji/heart-eyes-cat`
- `https://animatemojis.com/emoji/smirk-cat`
- `https://animatemojis.com/emoji/kissing-cat`
- `https://animatemojis.com/emoji/pouting-cat`
- `https://animatemojis.com/emoji/crying-cat-face`
- `https://animatemojis.com/emoji/scream-cat`
- `https://fonts.gstatic.com/s/e/notoemoji/latest/1f431/512.gif`
- `https://fonts.gstatic.com/s/e/notoemoji/latest/1f638/512.gif`
- `https://fonts.gstatic.com/s/e/notoemoji/latest/1f639/512.gif`
- `https://fonts.gstatic.com/s/e/notoemoji/latest/1f63a/512.gif`
- `https://fonts.gstatic.com/s/e/notoemoji/latest/1f63b/512.gif`
- `https://fonts.gstatic.com/s/e/notoemoji/latest/1f63c/512.gif`
- `https://fonts.gstatic.com/s/e/notoemoji/latest/1f63d/512.gif`
- `https://fonts.gstatic.com/s/e/notoemoji/latest/1f63e/512.gif`
- `https://fonts.gstatic.com/s/e/notoemoji/latest/1f63f/512.gif`
- `https://fonts.gstatic.com/s/e/notoemoji/latest/1f640/512.gif`

## SHA-256

```text
6d86848cd79a778976fde487317f2df4b92abd99b80a917e73000550bf419759  noto_cat_face_1f431.gif
501c46661342f3b60685e3783cf271abcb1034a3f0138e4add2c03cf869023e7  noto_crying_cat_1f63f.gif
f8385e385ce0f267c11ee72253156bc47851f59cbf5a6e89c5a63f5d712fbfbc  noto_heart_eyes_cat_1f63b.gif
07021dcc12be911b306977bba58d962940bcf72cdc95c4697784c205a5e56929  noto_joy_cat_1f639.gif
0f9a6760154b77b2ad831796067374ea26f615c12d6eb1c45136cc133f01a852  noto_kissing_cat_1f63d.gif
213a8695917d623ea576daf50e4c1d88c72e9b38fa7838b13d9bc6e0803fa10a  noto_pouting_cat_1f63e.gif
5d329b0aad3427658612eeebd23fafb33f527773343c7726540cdd935e1d28f2  noto_scream_cat_1f640.gif
dc1f04685b4c508a8a314bfa71a40fafb25c738d3fe900b5cbf767e934591dc3  noto_smile_cat_1f638.gif
a7f9731d10269fc732ca19e6e25d85bd84c92f1991b324fba15bea9db865aa01  noto_smiley_cat_1f63a.gif
54351b8098e30a1dce1ae4f5468f7bbcf9770b907a253046e1fd1eaefaf18c1b  noto_smirk_cat_1f63c.gif
3d32be0daf9b9904c78568cbda923210b89c5a9fc26a29e8c833ac6c592e1c9a  preview_contact_sheet.png
```

## Firmware Notes

- `LV_USE_GIF` is `0` in the current AmebaSmart LVGL config, so raw GIFs are
  not firmware-ready yet.
- Firmware-ready converted assets now live in `../noto_cat_lvgl/`. They are
  downsampled LVGL `lv_image_dsc_t` frame descriptors for `lv_animimg`
  playback, so the runtime does not depend on the GIF decoder or a filesystem.
