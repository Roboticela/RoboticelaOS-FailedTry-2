# Unicode Upgrade Workflow (Goal 3.14)

Normal runtime builds use **already-generated** tables. They never download
Unicode files and never require Python.

## Prerequisites

- Python 3
- Network access only when fetching (step 3)
- Official Unicode Consortium data (Unicode License)

## Steps

1. **Select versions** in `Tools/ucd/robo_unicode_version.py`
   - `UCD_VERSION_*` / `EMOJI_VERSION_*`
   - Bump `GENERATOR_VERSION` if pipeline semantics change
2. **Update `Tools/ucd/manifest.json`**
   - `unicode_version`, `emoji_version`, `base_url_*`
   - Every file `url` (official Unicode.org only)
   - Clear or refresh `sha256` after fetch (or fill after step 4)
3. **Download**
   ```powershell
   .\runtime\Tools\ucd\Fetch-Ucd.ps1
   ```
4. **Record checksums** (if URLs changed)
   - Compute SHA-256 of each `Tools/ucd/source/*`
   - Update `manifest.json` `sha256` fields
5. **Validate sources**
   ```powershell
   python .\runtime\Tools\ucd\generate_all.py --verify-only
   ```
6. **Generate all tables**
   ```powershell
   python .\runtime\Tools\ucd\generate_all.py
   ```
7. **Reproducibility**
   ```powershell
   python .\runtime\Tools\ucd\generate_all.py --repro
   ```
8. **Refresh golden hashes**
   ```powershell
   python .\runtime\Tools\ucd\generate_all.py --update-golden
   ```
9. **Verify pipeline**
   ```powershell
   python .\runtime\Tools\ucd\verify_pipeline.py --sizes
   ```
10. **Build & test (no Chromium)**
    ```powershell
    .\runtime\Tools\Build.ps1 -Configuration Debug -Verify -Test
    .\runtime\Tools\Build.ps1 -Configuration Release -Verify -Test
    .\runtime\Tools\Build.ps1 -Configuration Debug -Test -Sanitize
    ```
11. **Review** generated diffs (`*Data.h`, `data/robo_*_data.c`, `Tests/Data/*`)
12. **Update docs** (`unicode/README.md`, `PROGRESS.md`, attribution if needed)
13. **Commit** generated data + manifest + golden hashes together

## Fail loud

The pipeline must abort on:

- missing required sources
- SHA-256 mismatch
- version header mismatch vs pin
- generator errors
- golden hash drift (after `--check-golden` / `verify_pipeline.py`)

## Do not

- Edit generated `*Data.h` / `robo_*_data.c` by hand
- Mix Unicode versions across modules
- Download Unicode during `Build.ps1`
- Depend on Chromium / ICU tables
