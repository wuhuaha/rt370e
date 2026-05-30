# River LVGL Animation Assets

This folder contains firmware-ready LVGL `lv_animimg` resources generated
from selected GIFs in `../emoji_candidates/` and `../action_candidates/`.

- Target size: `80x80`
- Sampled frames per animation: `8`
- Pixel format: `LV_COLOR_FORMAT_ARGB8888` using LVGL BGRA byte order
- Runtime object: `lv_animimg`, so no runtime GIF decoder or filesystem is required

## Generated Animations

| Key | Caption | Source GIF | Source frames | Source duration | Sampled frame indices |
| --- | --- | --- | ---: | ---: | --- |
| `noto_smiley_cat_1f63a` | smiley cat | `emoji_candidates/noto_smiley_cat_1f63a.gif` | 61 | 2220ms | `0, 2, 11, 20, 30, 38, 42, 51` |
| `noto_smile_cat_1f638` | smile cat | `emoji_candidates/noto_smile_cat_1f638.gif` | 96 | 3150ms | `0, 8, 21, 34, 47, 60, 73, 86` |
| `noto_joy_cat_1f639` | joy cat | `emoji_candidates/noto_joy_cat_1f639.gif` | 88 | 2640ms | `0, 11, 22, 33, 44, 55, 66, 77` |
| `noto_heart_eyes_cat_1f63b` | heart eyes cat | `emoji_candidates/noto_heart_eyes_cat_1f63b.gif` | 77 | 2520ms | `0, 10, 21, 31, 42, 52, 63, 73` |
| `noto_smirk_cat_1f63c` | smirk cat | `emoji_candidates/noto_smirk_cat_1f63c.gif` | 53 | 2670ms | `0, 0, 3, 14, 25, 31, 31, 42` |
| `noto_kissing_cat_1f63d` | kissing cat | `emoji_candidates/noto_kissing_cat_1f63d.gif` | 63 | 2160ms | `0, 0, 9, 18, 27, 36, 45, 54` |
| `noto_pouting_cat_1f63e` | pouting cat | `emoji_candidates/noto_pouting_cat_1f63e.gif` | 57 | 2010ms | `0, 2, 10, 19, 27, 35, 44, 52` |
| `noto_crying_cat_1f63f` | crying cat | `emoji_candidates/noto_crying_cat_1f63f.gif` | 51 | 1530ms | `0, 6, 12, 19, 25, 31, 38, 44` |
| `noto_scream_cat_1f640` | scream cat | `emoji_candidates/noto_scream_cat_1f640.gif` | 55 | 2160ms | `0, 0, 1, 10, 19, 28, 37, 46` |
| `noto_cat_face_1f431` | cat face | `emoji_candidates/noto_cat_face_1f431.gif` | 75 | 2760ms | `0, 7, 19, 30, 42, 53, 65, 74` |
| `action_light_on` | light on | `action_candidates/light_bulb_on_off_commons.gif` | 5 | 620ms | `0, 1, 1, 2, 2, 3, 3, 4` |
| `action_light_off` | light off | `action_candidates/light_bulb_on_off_commons.gif` | 5 | 620ms | `0, 1, 1, 2, 2, 3, 3, 4` |
| `action_curtain_open` | curtain open | `action_candidates/curtain_open_close_commons.gif` | 87 | 1840ms | `0, 11, 23, 34, 46, 57, 69, 80` |
| `action_curtain_close` | curtain close | `action_candidates/curtain_open_close_commons.gif` | 87 | 1870ms | `0, 11, 23, 35, 46, 58, 70, 81` |

## Regeneration

```bash
cd /root/ameba-river
python3 components/river_ui/assets/noto_cat_lvgl/generate_noto_cat_lvgl.py
```
