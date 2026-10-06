# Embedded artwork

`loading.jpg` and `logoff.jpg` are the accepted 800×480 lifecycle backgrounds.
They are committed build inputs; no image tooling or external artwork checkout
is needed to compile the application.

The project artwork was generated and edited with OpenAI image generation.
The accepted edits removed baked-in status text and progress indicators so the
application can render live status. The JPEGs were exported with Pillow 12.3.0,
quality 94, 4:4:4 chroma, baseline encoding. Drafts and unused concepts are not
part of this source distribution.

ESP-IDF's JPEG driver decodes into a reusable RGB565 PSRAM buffer. PAX renders
the background and live status overlay; failures fall back to built-in text.

Font sources, generated bitmap data and their SIL Open Font License notices are
in `fonts/` and `../render/font_assets.c`. Function-key icon sources and their
MIT notice are in `keys/`; the generated data is `../render/key_icons.h`.
