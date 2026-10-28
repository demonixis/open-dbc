# Branding

`resources/editor.png` is the source of the OpenDBC visual identity: a neon cyan
pyramid filled with gold code. `scripts/generate_branding.py` derives every image
in `resources/branding/` from it; edit the script, not the images.

| File | Use |
| --- | --- |
| `logo_mark.png` | Pyramid only, transparent |
| `logo_horizontal.png`, `logo_stacked.png` | Pyramid and wordmark, transparent, for dark backgrounds |
| `logo_horizontal_light_bg.png`, `logo_stacked_light_bg.png` | Same without glow, for light backgrounds |
| `header_x_1500x500.png` | X / Mastodon / Bluesky header; content kept clear of the avatar |
| `header_linkedin_1584x396.png` | LinkedIn banner |
| `header_facebook_1640x624.png` | Facebook cover |
| `header_youtube_2560x1440.png` | YouTube banner; content inside the 1546x423 safe area |
| `social_preview_github_1280x640.png` | GitHub repository social preview |
| `wallpaper_*_1920x1080.png` | Full HD wallpapers: horizon, minimal, code |

Colours: cyan `#28C8E6` (glow), `#E8FFFF` (neon core), gold `#F5BE3C`, ice
`#E6FAFC`, navy `#0A1822`. The wordmark is Avenir Next Heavy, "Open" in ice
(navy on light backgrounds) and "DBC" in gold.

Regenerate on macOS (system Avenir Next and Menlo fonts) with Pillow and NumPy:

```sh
python3 -m venv .venv-branding
.venv-branding/bin/pip install pillow numpy
.venv-branding/bin/python scripts/generate_branding.py
```
