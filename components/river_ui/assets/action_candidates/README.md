# Action GIF Candidates

This folder stores source GIFs used to generate firmware-ready LVGL action
animations. Runtime playback still uses generated `lv_animimg` frame tables; the
GIF decoder and filesystem are not needed on the board.

## Sources

| Local file | Use | Source | Author | License |
| --- | --- | --- | --- | --- |
| `light_bulb_on_off_commons.gif` | `action_light_on`, `action_light_off` | Wikimedia Commons `File:Gluhbirne.gif` | Andrikkos | CC BY-SA 3.0 |
| `curtain_open_close_commons.gif` | `action_curtain_open`, `action_curtain_close` | Wikimedia Commons `File:Guillotine-curtain.gif` | KDS444 | CC BY-SA 3.0 |

Source pages:
- https://commons.wikimedia.org/wiki/File:Gl%C3%BChbirne.gif
- https://commons.wikimedia.org/wiki/File:Guillotine-curtain.gif
