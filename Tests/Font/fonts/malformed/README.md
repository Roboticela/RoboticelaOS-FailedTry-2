# Malformed fixtures

| File | Purpose | Expected |
|---|---|---|
| `truncated_sfnt.bin` | TrueType signature + truncated directory | SFNT parse / validate fails safely |
| `empty.bin` | Zero-length input | Rejected / no crash |

Provenance: original RoboticelaOS synthetic bytes (CC0).
Mirrored in `SuiteRoboFontConformance.c` as `k_corpus_*`.
