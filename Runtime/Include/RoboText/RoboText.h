/*
 * RoboText — the runtime's text layer (Phase 12.4).
 *
 * The module between a document's text and the two things that consume it, and the answer to a question
 * this tree had been answering in four places: **what is a piece of text, once, as an object?**
 *
 *     text ── RoboTextBuffer ── RoboTextRunSet ── RoboTextShapeResult ──┬─ measurement → RoboLayout
 *              (units, ranges)   (runs, fonts)     (glyph runs, mapping) └─ glyph runs  → RoboPaint
 *
 * ## The three rules this module exists to state
 *
 * **A scalar is not a byte, and a glyph is not a scalar.** `RoboTextByteIndex`, `RoboTextScalarIndex`,
 * `RoboTextGlyphIndex` and `RoboTextClusterIndex` are four types because four things are being counted,
 * and the conversions between them are functions with a `Result` rather than arithmetic at a call site.
 *
 * **Glyph selection and advance accumulation happen here, not in a painter.** The path this phase
 * replaces looked a code point up in the cmap, drew it, and advanced the pen — inside the paint pass.
 * A painter that selects glyphs is a painter that cannot be given a shaped run.
 *
 * **A text layer is not a font parser and not a Unicode database.** Faces come from RoboFont; script
 * classification, bidi classes and encoding come from RoboUnicode (Phase 4). RoboText owns the model,
 * the runs, the glyph record and the mapping, and nothing below them.
 *
 * ## What this build does not do, and where each absence is recorded
 *
 * Substitution, positioning, kerning, ligatures, the bidi algorithm, complex scripts, vertical text and
 * variable-axis application are all **absent**, and each is a documented flag in
 * `RoboTextVersion.h` rather than a claim in prose. The one that needs saying in this file is the
 * deliberate placeholder at the centre of it: `RoboTextGlyphGenerationCmapPerScalar` is one `cmap`
 * lookup per scalar in logical order, which is *correct for the text it handles* and is **not shaping**.
 * It is isolated behind `RoboTextGlyphRunGenerate` so that Phase 12.10's substitution pass replaces one
 * function rather than the architecture.
 *
 * ## Boundaries
 *
 * RoboText may include `RoboBase`, `RoboMem`, `RoboUnicode` and `RoboFont`. It may **not** include
 * `RoboHTML`, `RoboCSS`, `RoboLayout`, `RoboPaint`, `RoboRender`, `RoboWindow`, `RoboDisplay`,
 * `RoboGraphics` or `RoboTypography` — the last because the text layer must sit *under* the layers that
 * use it, and a typography layer above it is a consumer of text rather than a dependency of it.
 * `Assert-RoboTextBoundaries` in `Runtime/Tools/Build.ps1` enforces the list.
 *
 * Contract and limitations: `Documentation/Runtime/Text/Architecture.md`.
 */
#ifndef ROBO_TEXT_H
#define ROBO_TEXT_H

#include "RoboText/RoboTextAnalysis.h"
#include "RoboText/RoboTextBuffer.h"
#include "RoboText/RoboTextDiagnostics.h"
#include "RoboText/RoboTextFont.h"
#include "RoboText/RoboTextFontSelection.h"
#include "RoboText/RoboTextGlyphs.h"
#include "RoboText/RoboTextKerning.h"
#include "RoboText/RoboTextLigature.h"
#include "RoboText/RoboTextRuns.h"
#include "RoboText/RoboTextShape.h"
#include "RoboText/RoboTextTypes.h"
#include "RoboText/RoboTextDirection.h"
#include "RoboText/RoboTextBidi.h"
#include "RoboText/RoboTextUnicode.h"
#include "RoboText/RoboTextVersion.h"

#endif /* ROBO_TEXT_H */
