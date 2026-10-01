# Simple artwork from real captures

`build_clean_artwork.py` creates a 630 × 500 cover and up to five consistent
1600 × 1000 store images. It uses plain white surfaces, clean typography,
small labels and the supplied real pixels. It does not draw an interface,
capture the application, generate images with AI or publish anything.

Use Python 3 with Pillow. Segoe UI is read from the host's Windows Fonts folder;
`--font` and `--font-bold` accept other explicit font files. Keep the original
captures, source configuration and detailed audit outside the public source
repository.

```powershell
python marketing/tools/build_clean_artwork.py `
  --config ../capture-config.json `
  --output marketing/itchio-clean-v0.1.0 `
  --audit ../artwork-audit.json
```

The tool refuses an existing output folder. Older artwork remains untouched.
To revise a set, choose a fresh preview directory, inspect every output, then
create a new reviewed destination.

The private JSON configuration contains `cover`, `scenes` and an optional
`unrepresented_scenes` list. Every source record requires:

- `path`: the original single-frame PNG, JPEG or WebP path.
- `sha256`: the hash checked after inspecting that original.
- `kind`: `application-capture` or `native-score-export`.
- `build`: the build that produced the source, with an optional
  `executable_sha256` for exact provenance.
- `reviewed`: the prior inspection record.
- `crop`: optional source-pixel bounds `[left, top, right, bottom]`.

Each scene also needs a short English `title` and lowercase English `slug`.
The tool permits one to five available scenes; missing workflows are omitted
rather than replaced by invented screens. A native score export is explicitly
labeled as an export, so it cannot be mistaken for an application screenshot.

Before composition, inspect the actual originals for personal information,
unrelated windows, dialogs, untranslated labels and cursor obstructions. Select
safe crops from the real application or score. The entire selected rectangle
is preserved with contain fitting, without stretching or enlargement. Sources
are never recolored or reskinned. Inspect every final PNG for readable text,
clipping, misleading content and private information.

The private audit records original paths and detected formats, source/build hashes, crop and
placement coordinates, output dimensions/hashes, font hashes and Pillow
version. Each composition is rendered twice and compared byte for byte.
New RGB canvases discard source metadata. Only reviewed artwork and its public
description belong in the store asset directory.

MillerScore is an independent GPLv3 community fork; these assets must not imply
official affiliation with Muse Group. Publishing remains a separate action.
