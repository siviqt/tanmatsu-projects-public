# Menu artwork source

`menu-background-master.png` is the selected text-free fleet/planet illustration
used beneath live menus and deployment surfaces. It is source material; firmware
embeds the 800×480 `components/spacebattles/assets/menu.jpg`.

Run `python3 tools/prepare-cyberdeck-assets.py` with Pillow to regenerate that JPEG
(quality 88, 4:4:4, baseline). Conversion is not required to build the application.
The title mask in `components/spacebattles/render/title.h` is a required source
asset: its original raster master was absent from the supplied project.
