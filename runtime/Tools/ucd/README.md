# Unicode Character Database tooling

## Version

**Unicode 16.0.0** (matches `ROBO_UNICODE_VERSION_STRING`).

## License

Unicode Data Files are under the Unicode License Agreement.
See `UNICODE_DATA_LICENSE.txt` and https://www.unicode.org/license.txt

## Fetch sources

```powershell
.\runtime\Tools\ucd\Fetch-Ucd.ps1
```

Downloads official UCD files into `Tools/ucd/source/` (gitignored).

## Generate runtime tables

```powershell
python .\runtime\Tools\ucd\generate_ucd.py
python .\runtime\Tools\ucd\validate_script.py
python .\runtime\Tools\ucd\generate_norm.py
python .\runtime\Tools\ucd\generate_grapheme.py
python .\runtime\Tools\ucd\generate_case.py
python .\runtime\Tools\ucd\generate_word.py
python .\runtime\Tools\ucd\generate_sentence.py
python .\runtime\Tools\ucd\generate_linebreak.py
python .\runtime\Tools\ucd\generate_bidi.py
```

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

### Word segmentation tables (`generate_word.py`)

| Output | Role |
|---|---|
| `Include/RoboUnicode/RoboWordData.h` | Word_Break enums + lookup decls |
| `Source/RoboUnicode/data/robo_word_data.c` | Compressed Word_Break range tables |
| `Tests/Data/robo_word_conformance.inc` | Official `WordBreakTest.txt` vectors |

### Sentence segmentation tables (`generate_sentence.py`)

| Output | Role |
|---|---|
| `Include/RoboUnicode/RoboSentenceData.h` | Sentence_Break enums + lookup decls |
| `Source/RoboUnicode/data/robo_sentence_data.c` | Compressed Sentence_Break range tables |
| `Tests/Data/robo_sentence_conformance.inc` | Official `SentenceBreakTest.txt` vectors |

### Line breaking tables (`generate_linebreak.py`)

| Output | Role |
|---|---|
| `Include/RoboUnicode/RoboLineData.h` | Line_Break enums + lookup decls |
| `Source/RoboUnicode/data/robo_line_data.c` | Compressed Line_Break range tables |
| `Tests/Data/robo_line_conformance.inc` | Official `LineBreakTest.txt` vectors |

### Bidi tables (`generate_bidi.py`)

| Output | Role |
|---|---|
| `Include/RoboUnicode/RoboBidiData.h` | Bidi_Class enums + bracket lookup |
| `Source/RoboUnicode/data/robo_bidi_data.c` | Compressed Bidi_Class + BidiBrackets |
| `Tests/Data/robo_bidi_character_conformance.bin` | Packed `BidiCharacterTest.txt` (91,707) |
| `Tests/Data/robo_bidi_character_conformance.h` | Case-count / magic metadata |

Reference oracle: `bidi_ref.py` (also runs abstract `BidiTest.txt`).

Do not hand-edit generated files. Re-run after upgrading Unicode version
(update URLs in `Fetch-Ucd.ps1` and `UCD_VERSION` in all generators).

## Inputs used

### Properties (`generate_ucd.py`)

- `UnicodeData.txt` — General_Category, Canonical_Combining_Class
- `Scripts.txt` — Script
- `EastAsianWidth.txt` — East_Asian_Width
- `DerivedCoreProperties.txt` — Alphabetic, Uppercase, Lowercase, Default_Ignorable_Code_Point
- `PropList.txt` — White_Space
- `emoji/emoji-data.txt` — Emoji, Emoji_Presentation, Extended_Pictographic

### Normalization (`generate_norm.py`)

- `UnicodeData.txt` — field 5 decomposition mappings (canonical + compatibility)
- `CompositionExclusions.txt` — characters excluded from composition
- `NormalizationTest.txt` — conformance test vectors (optional but recommended)
- `DerivedNormalizationProps.txt` — fetched for completeness; Hangul uses algorithmic rules at runtime

Composition pairs require CCC(first character) == 0 (from `UnicodeData.txt`).

### Grapheme segmentation (`generate_grapheme.py`)

- `auxiliary/GraphemeBreakProperty.txt` — Grapheme_Cluster_Break
- `DerivedCoreProperties.txt` — Indic_Conjunct_Break (GB9c)
- `auxiliary/GraphemeBreakTest.txt` — conformance vectors

### Case mapping (`generate_case.py`)

- `UnicodeData.txt` — simple upper/lower/title (fields 12–14)
- `SpecialCasing.txt` — full unconditional mappings + Final_Sigma
- `CaseFolding.txt` — statuses C/F/S (T skipped for locale-independent fold)
- `DerivedCoreProperties.txt` — `Cased`, `Case_Ignorable`

### Word segmentation (`generate_word.py`)

- `auxiliary/WordBreakProperty.txt` — `Word_Break` property
- `auxiliary/WordBreakTest.txt` — conformance vectors
- Extended_Pictographic for WB3c comes from existing UCD emoji tables at runtime

### Sentence segmentation (`generate_sentence.py`)

- `auxiliary/SentenceBreakProperty.txt` — `Sentence_Break` property
- `auxiliary/SentenceBreakTest.txt` — conformance vectors

### Line breaking (`generate_linebreak.py`)

- `LineBreak.txt` — `Line_Break` property
- `auxiliary/LineBreakTest.txt` — conformance vectors
- Runtime also uses existing UCD Gc / East_Asian_Width / Extended_Pictographic

### Bidirectional Algorithm (`generate_bidi.py`)

- `extracted/DerivedBidiClass.txt` — `Bidi_Class`
- `BidiBrackets.txt` — paired brackets for N0
- `BidiMirroring.txt` — fetched for completeness (glyph mirroring later)
- `BidiCharacterTest.txt` — packed into `.bin` conformance fixture
- `BidiTest.txt` — abstract-class suite (Python `bidi_ref.py`)

### Script / Script_Extensions (`generate_ucd.py`)

- `Scripts.txt` — primary `Script` property (all scripts)
- `ScriptExtensions.txt` — `Script_Extensions` multi-script sets
- `PropertyValueAliases.txt` — ISO 15924 short names (`sc ; Arab ; Arabic`)
- Validator: `validate_script.py`
- Fixture: `Tests/Data/robo_script_conformance.bin`
