# Tanmatsu function keys

Original `fat/tanmatsu/icons/f1.png` through `f6.png` from the local Tanmatsu launcher SDK checkout. MIT license, Copyright (c) 2025 Nicolai Electronics; see LICENSE.

Order: red cross, orange triangle, yellow-green square, green circle, cyan club, magenta diamond. The renderer pairs each with its F-number and action, and uses a light backing to preserve contrast without recoloring the pixels.

`tools/prepare-key-icons.py` regenerates `render/key_icons.h` from these PNGs using Pillow. The indexed representation retains all original RGBA pixels and avoids adding a runtime PNG decoder or external asset dependency.
