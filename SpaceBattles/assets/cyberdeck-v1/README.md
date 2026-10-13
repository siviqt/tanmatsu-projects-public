# Cyberdeck production artwork

`menu-background-master.png` is the selected generated fleet/planet background.
It is source material for the native 800x480 JPEG embedded in the firmware.
Live panels, branding, radar grid, ships, focus and controls are drawn by PAX.
Private handovers clear the framebuffer.

From the project root, run `python3 tools/prepare-cyberdeck-assets.py` with Pillow
to reproduce `components/spacebattles/assets/menu.jpg` (quality 88, 4:4:4,
baseline JPEG). Regeneration is optional; the JPEG is included.

The title mask in `components/spacebattles/render/title.h` is a self-contained
build input. Its original raster master is unavailable; the converter does not
regenerate that mask.
