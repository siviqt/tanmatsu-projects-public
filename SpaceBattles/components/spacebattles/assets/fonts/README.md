# M5 embedded typography

Sources retrieved 2026-10-02 from Google Fonts:

- `https://raw.githubusercontent.com/google/fonts/main/ofl/oxanium/Oxanium%5Bwght%5D.ttf`
- `https://raw.githubusercontent.com/google/fonts/main/ofl/spacemono/SpaceMono-Regular.ttf`
- Each directory's `OFL.txt`, retained here as `Oxanium-OFL.txt` and `SpaceMono-OFL.txt`.

The unmodified source fonts and their copyright/license notices are bundled here.
Generated bitmap derivatives use internal names prefixed `SB` and retain the OFL.

SHA-256 inputs (also enforced by the converter):

```
2ce01d946e1e1ffc8d7eecfffbda8623bedd63eaf811a20488c4b69af45babb0  Oxanium.ttf
95837e182baeeada83368f7748db28357f0a1b75c6b84ff7065b5edf933c8e18  SpaceMono-Regular.ttf
```

Run `python3 tools/prepare-m5-fonts.py` from the application root with Pillow
12.3.0 / FreeType 2.14.3. `--check` regenerates in memory and verifies the checked-in
C output byte for byte. No network or TTF parser is needed by firmware builds.
Pillow uses BASIC layout, variable weight axis 500/600/700 for Oxanium and Regular
400 for Space Mono. ASCII U+0020–U+007E is the initial English UI glyph range.
Use ASCII hyphens in UI text; additional languages/glyphs require regeneration.

Each glyph is rasterized at its native role/profile size, rounded advance width,
with baseline placement from FreeType ascent. Coverage is quantized to four
levels, packed low bits first, four pixels per byte, each row byte-aligned, in
PAX `BITMAP_VAR` format. Nineteen deduplicated strikes cover all roles. Bitmap
arrays and descriptors reside in flash; rendering needs no font heap allocation.

| Role | Family / weight | Small | Medium | Large |
|---|---|---:|---:|---:|
| Caption | Oxanium Medium 500 | 14 | 18 | 24 |
| Body | Oxanium Medium 500 | 16 | 20 | 26 |
| Button | Oxanium SemiBold 600 | 16 | 20 | 26 |
| Heading | Oxanium Bold 700 | 24 | 28 | 34 |
| Timer | Space Mono Regular 400 | 20 | 24 | 30 |
| Coordinate | Space Mono Regular 400 | 16 | 20 | 26 |
| Telemetry | Space Mono Regular 400 | 14 | 18 | 24 |

The built-in Sky Mono font remains available for fallback and the retained M3
input diagnostic. Dynamic lifecycle status/error text uses the new semantic fonts;
the pre-existing JPEG title artwork is unchanged.
