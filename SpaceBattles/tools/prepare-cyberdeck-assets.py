#!/usr/bin/env python3
"""Reproduce the native menu JPEG from the preserved production master."""
from pathlib import Path
from PIL import Image
root = Path(__file__).resolve().parents[1]
with Image.open(root/'assets/cyberdeck-v1/menu-background-master.png') as image:
    image.convert('RGB').resize((800,480),Image.Resampling.LANCZOS).save(
        root/'components/spacebattles/assets/menu.jpg',quality=88,subsampling=0,progressive=False)
