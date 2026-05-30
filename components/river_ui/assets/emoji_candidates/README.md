# Emoji Candidate Assets

This folder holds downloaded animated cat emoji candidates for the Orvibo LVGL
emoji display follow-up. These files are raw candidate assets only; they are not
yet converted into LVGL image descriptors or selected for firmware playback.

## Downloaded Assets

| File | Source | License noted by source | Size / frames | Selection note |
| --- | --- | --- | --- | --- |
| `noto_smiley_cat_1f63a.gif` | AnimatEmojis / Google Noto animated emoji, `1f63a` | CC BY 4.0 | 512x512, 61 frames, 516429 bytes | Official animated emoji source, permissive attribution license. |
| `noto_smile_cat_1f638.gif` | AnimatEmojis / Google Noto animated emoji, `1f638` | CC BY 4.0 | 512x512, 96 frames, 931511 bytes | Official animated emoji source, expressive happy cat. |
| `noto_heart_eyes_cat_1f63b.gif` | AnimatEmojis / Google Noto animated emoji, `1f63b` | CC BY 4.0 | 512x512, 77 frames, 880540 bytes | Official animated emoji source, high-cuteness reaction candidate. |
| `pixabay_cat_cute_emoji_6939.gif` | Pixabay GIF 6939 | Pixabay Content License | 170x170, 20 frames, 56699 bytes | Compact cute candidate; Pixabay page showed roughly 7k views, 3k downloads, and 64 saves at download time. |
| `pixabay_cat_cute_tickle_6937.gif` | Pixabay GIF 6937 | Pixabay Content License | 170x170, 20 frames, 77956 bytes | Compact cute candidate; Pixabay page showed roughly 7k views, 3.5k downloads, and 63 saves at download time. |
| `preview_contact_sheet.png` | Local preview sheet | Derived from downloaded candidates | 240x800 PNG | First-frame preview for quick visual comparison only. |

## Source URLs

- `https://animatemojis.com/emoji/smiley-cat`
- `https://animatemojis.com/emoji/smile-cat`
- `https://animatemojis.com/emoji/heart-eyes-cat`
- `https://fonts.gstatic.com/s/e/notoemoji/latest/1f63a/512.gif`
- `https://fonts.gstatic.com/s/e/notoemoji/latest/1f638/512.gif`
- `https://fonts.gstatic.com/s/e/notoemoji/latest/1f63b/512.gif`
- `https://pixabay.com/gifs/cat-cute-emoji-tickle-6937/`
- `https://pixabay.com/gifs/cat-cute-emoji-pet-6939/`
- `https://cdn.pixabay.com/animation/2023/07/02/16/21/16-21-16-410_512.gif`
- `https://cdn.pixabay.com/animation/2023/07/02/16/21/16-21-18-411_512.gif`

## SHA-256

```text
f8385e385ce0f267c11ee72253156bc47851f59cbf5a6e89c5a63f5d712fbfbc  noto_heart_eyes_cat_1f63b.gif
dc1f04685b4c508a8a314bfa71a40fafb25c738d3fe900b5cbf767e934591dc3  noto_smile_cat_1f638.gif
a7f9731d10269fc732ca19e6e25d85bd84c92f1991b324fba15bea9db865aa01  noto_smiley_cat_1f63a.gif
52345baace9b56f0e494b1caad8bb17d91c0058a39124cb5388f8ef38d33f210  pixabay_cat_cute_emoji_6939.gif
252b542931211b7b76e866365f6cf0055505c75c99be5c2dca53be40e1c18dc6  pixabay_cat_cute_tickle_6937.gif
45442aecc4cd97b4bd003a1848ec0d35796b36772211aef3f4de2546dca07547  preview_contact_sheet.png
```

## Firmware Notes

- `LV_USE_GIF` is `0` in the current AmebaSmart LVGL config, so raw GIFs are
  not firmware-ready yet.
- The likely next step is to select one compact candidate, resize/crop to the
  display target, decode frames offline, and generate project-owned LVGL image
  descriptors for `lv_animimg` playback. This embeds the asset into the image
  and avoids a runtime filesystem dependency.
