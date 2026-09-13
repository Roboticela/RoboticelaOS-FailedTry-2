# Unicode Data Attribution

Roboticela Runtime embeds **Unicode Character Database** and **Emoji / UTS #51**
data derived from official Unicode Consortium files.

## License

Unicode Data Files are licensed under the Unicode License Agreement.

See:

- `runtime/Tools/ucd/UNICODE_DATA_LICENSE.txt`
- https://www.unicode.org/license.txt

This is **not** the same as the Roboticela Runtime source license. Redistribution
of the derived tables must continue to comply with the Unicode License.

## Versions currently used

| Product | Version |
|---|---|
| Unicode Standard / UCD | 16.0.0 |
| Emoji (UTS #51 data) | 16.0 |

Pinned in `Tools/ucd/robo_unicode_version.py` and
`Include/RoboUnicode/RoboUnicodeVersion.h`.

## Provenance

Files are fetched from official URLs listed in `Tools/ucd/manifest.json`
(for example `https://www.unicode.org/Public/16.0.0/ucd/...` and
`https://www.unicode.org/Public/emoji/16.0/...`).

SHA-256 checksums in the manifest identify the exact inputs used to generate
committed runtime tables.

## What is generated

Compact Roboticela runtime tables under:

- `Include/RoboUnicode/*Data.h`
- `Source/RoboUnicode/data/robo_*_data.c`
- `Tests/Data/robo_*_conformance.*`

These are **derived works** of the Unicode Data Files, produced by
`Tools/ucd/generate_*.py`. They are not Chromium or ICU copies.

## Contact / notices

Copyright © Unicode, Inc. Unicode and the Unicode Logo are registered
trademarks of Unicode, Inc. in the U.S. and other countries.
