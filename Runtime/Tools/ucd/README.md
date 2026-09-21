# Unicode Character Database tooling

## Version

**Unicode 16.0.0** / **Emoji 16.0** — pinned in `robo_unicode_version.py`
(synced to `Include/RoboUnicode/RoboUnicodeVersion.h`).

Do not scatter version literals across generators.

## License

Unicode Data Files are under the Unicode License Agreement.
See `UNICODE_DATA_LICENSE.txt` and `../../Docs/UnicodeDataAttribution.md`.

## Offline builds

`Build.ps1` uses **committed** generated tables. It never downloads Unicode
data and never invokes these scripts. Regeneration is an explicit developer step.

## Fetch sources

```powershell
.\runtime\Tools\ucd\Fetch-Ucd.ps1
```

Downloads official files listed in `manifest.json` into `source/` (gitignored)
and verifies SHA-256 checksums.

## Generate / verify

```powershell
python .\runtime\Tools\ucd\generate_all.py              # all generators
python .\runtime\Tools\ucd\generate_all.py --verify-only
python .\runtime\Tools\ucd\generate_all.py --repro      # deterministic check
python .\runtime\Tools\ucd\generate_all.py --update-golden
python .\runtime\Tools\ucd\verify_pipeline.py --sizes
python .\runtime\Tools\ucd\measure_tables.py
```

Upgrade workflow: `../../unicode/UPGRADE.md`  
Audit: `../../unicode/AUDIT.md`

### Key files

| File | Role |
|---|---|
| `robo_unicode_version.py` | Single Unicode/Emoji/generator version pin |
| `manifest.json` | Official URLs, SHA-256, roles, generated outputs |
| `golden_hashes.json` | SHA-256 of committed generated artifacts |
| `ucd_common.py` | Shared validation / hashing helpers |
| `generate_all.py` | Orchestrator |
| `verify_pipeline.py` | Source + golden verification |
| `generate_*.py` | Per-subsystem table generators |
| `validate_script.py` | Script/SCX independent check |
| `bidi_ref.py` / `lb_ref.py` | Python oracles |

### Property tables (`generate_ucd.py`)

| Output | Role |
|---|---|
| `Include/RoboUnicode/RoboUcdData.h` | Enums + Script/SCX lookup decls |
| `Source/RoboUnicode/data/robo_ucd_data.c` | Compressed range tables + SCX pools |
| `Tests/Data/robo_script_conformance.bin` | Script + Script_Extensions validation |

### Normalization tables (`generate_norm.py`)

| Output | Role |
|---|---|
| `Include/RoboUnicode/RoboNormData.h` | Decomposition/composition table types |
| `Source/RoboUnicode/data/robo_norm_data.c` | Canon + compat one-step maps, composition pairs |
| `Tests/Data/robo_norm_conformance.inc` | Official `NormalizationTest.txt` vectors |

### Grapheme tables (`generate_grapheme.py`)

| Output | Role |
|---|---|
| `Include/RoboUnicode/RoboGraphemeData.h` | GB + InCB enums and lookup decls |
| `Source/RoboUnicode/data/robo_grapheme_data.c` | Compressed range tables |
| `Tests/Data/robo_grapheme_conformance.inc` | Official `GraphemeBreakTest.txt` vectors |

### Case tables (`generate_case.py`)

| Output | Role |
|---|---|
| `Include/RoboUnicode/RoboCaseData.h` | Simple/full/fold lookup decls |
| `Source/RoboUnicode/data/robo_case_data.c` | Mapping tables + Cased/Case_Ignorable |
| `Tests/Data/robo_case_conformance.inc` | CaseFolding + SpecialCasing fixtures |

### Word / sentence / line

Same pattern: `generate_word.py`, `generate_sentence.py`, `generate_linebreak.py`
→ `*Data.h`, `robo_*_data.c`, `robo_*_conformance.inc`.

### Bidi tables (`generate_bidi.py`)

| Output | Role |
|---|---|
| `Include/RoboUnicode/RoboBidiData.h` | Bidi_Class enums + bracket lookup |
| `Source/RoboUnicode/data/robo_bidi_data.c` | Compressed Bidi_Class + BidiBrackets |
| `Tests/Data/robo_bidi_character_conformance.bin` | Packed `BidiCharacterTest.txt` |
| `Tests/Data/robo_bidi_character_conformance.h` | Case-count / magic metadata |

### Emoji / UTS #51 tables (`generate_emoji.py`)

| Output | Role |
|---|---|
| `Include/RoboUnicode/RoboEmojiData.h` | Sequence kinds + trie decls |
| `Source/RoboUnicode/data/robo_emoji_data.c` | Compact RGI code-point trie |
| `Tests/Data/robo_emoji_conformance.bin` | Packed fully-qualified `emoji-test.txt` |

Code-point emoji booleans remain in `generate_ucd.py` (shared UCD).

Do not hand-edit generated files. Re-run after upgrading Unicode version
(update `robo_unicode_version.py` + `manifest.json`).

## Compact representations

Documented in `manifest.json` → `representations`:

- Sparse properties: compressed inclusive ranges + binary search
- Script_Extensions: deduplicated uint16 pools
- Normalization: decomp maps + composition pairs (Hangul algorithmic)
- Emoji RGI: code-point trie (longest match)

## Inputs

See `manifest.json` `files[]` for the complete list with URLs and checksums.
