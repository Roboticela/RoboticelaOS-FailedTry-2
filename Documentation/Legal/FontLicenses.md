# Font licenses and attribution

Every font redistributed with RoboticelaOS is listed here with its source,
version, license and required attribution. A face may only be added to
`System/Fonts/` when it has a row in this table **and** its license text ships
under `System/Fonts/licenses/`.

Licenses are never modified, and attribution is never removed.

## Inter

| | |
|---|---|
| Family | Inter |
| Upstream | The Inter Project Authors — <https://github.com/rsms/inter> |
| Version | 4.000 |
| Files | `System/Fonts/UI/Inter.ttf` (installed at `/System/Fonts/UI/Inter.ttf`) |
| Role in RoboticelaOS | initial system typeface: UI Display, UI Text and Monospace roles |
| License | SIL Open Font License, Version 1.1 (`OFL-1.1`) |
| License text | `System/Fonts/licenses/Inter-OFL.txt` |
| Copyright | Copyright 2020 The Inter Project Authors |

The OFL grants permission to use, study, modify and redistribute the font, and
requires that the copyright notice and license accompany every copy
(condition 2) and that no derivative use the Reserved Font Name without
permission (condition 3). The face is redistributed **unmodified**, so the
Reserved Font Name restriction is not engaged; the license text is included
verbatim.

### What Inter is not

RoboticelaOS must not copy proprietary Apple typefaces or font files. Inter is an
independent open-source face; it is **not** Apple's SF and is never named,
presented or documented as such. The design *target* is SF-class metrics,
proportions and spacing — a quality goal, not a claim of identity. Inter is not
renamed, and no metadata is falsified.

## Adding a face

1. Verify the license permits redistribution with the operating system.
2. Place the font under `System/Fonts/` (a role subdirectory such as `UI/`).
3. Place the license text under `System/Fonts/licenses/`.
4. Add an entry to `System/Fonts/system-fonts.manifest`.
5. Add a row to this file with source, version, license and attribution.
6. Do **not** rename a third-party face in a way that misrepresents it.
