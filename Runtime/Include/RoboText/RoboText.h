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
 * Variable-axis application is **absent**, and it is a documented flag in `RoboTextVersion.h` rather than a
 * claim in prose. Everything the earlier phases named as future work has since landed: substitution (12.10),
 * positioning (12.11), ligatures (12.12), kerning (12.13), the bidi algorithm (12.8), complex scripts
 * (12.19) and vertical writing (12.20). The placeholder this file used to describe —
 * `RoboTextGlyphGenerationCmapPerScalar`, one `cmap` lookup per scalar in logical order — is still the
 * **first** stage of `RoboTextGlyphRunGenerate`, and the substitution and positioning passes built on top of
 * it are what replaced the plan of "one function": the generator now runs cmap, then `vert`/`vrt2` and every
 * other caller feature through 12.10's GSUB, then 12.11's GPOS.
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

/*
 * The aggregate, and the answer to "what may a consumer include?" (Phase 12.24).
 *
 * **All of it, and the order is alphabetical.** That is the whole rule, and it is worth stating because the
 * alternative -- an aggregate that exposes a subset and leaves the rest to be reached for one header at a
 * time -- is how a tree ends up with two text APIs on one commit. `Runtime/Include/RoboText/` is the module's
 * public interface (the repository rule), so the aggregate names every header in it and a consumer that
 * writes `#include "RoboText/RoboText.h"` has the same surface as one that reads this file.
 *
 * Two *tiers* live inside it, and `Documentation/Runtime/Text/APIStability.md` is where the difference is
 * written down:
 *
 *   - the **seam** a document or a shell consumes -- the buffer, the runs, shaping, the glyph runs, the
 *     measurement, the selection map, the editor, the version record. Stable, and the only tier a new
 *     subsystem should need.
 *   - the **engines** behind it -- `RoboTextGsub.h`, `RoboTextGpos.h`, `RoboTextOtLayout.h`, the complex-script
 *     plans and the script helpers. Public because the conformance suites and this module's own sources are
 *     their callers, and stable as *records*, but no consumer of text should be running a substitution pass by
 *     hand: `Assert-RoboTextIntegrationRules` in `Runtime/Tools/Build.ps1` fails the build if a module above
 *     RoboText includes one, and the glyph run is what that module wants instead.
 */
#include "RoboText/RoboTextAnalysis.h"
#include "RoboText/RoboTextArabic.h"
#include "RoboText/RoboTextBidi.h"
#include "RoboText/RoboTextBuffer.h"
#include "RoboText/RoboTextComplex.h"
#include "RoboText/RoboTextDiagnostics.h"
#include "RoboText/RoboTextDirection.h"
#include "RoboText/RoboTextEditing.h"
#include "RoboText/RoboTextFont.h"
#include "RoboText/RoboTextFontSelection.h"
#include "RoboText/RoboTextGlyphs.h"
#include "RoboText/RoboTextGpos.h"
#include "RoboText/RoboTextGsub.h"
#include "RoboText/RoboTextIndic.h"
#include "RoboText/RoboTextKerning.h"
#include "RoboText/RoboTextLigature.h"
#include "RoboText/RoboTextMeasurement.h"
#include "RoboText/RoboTextOtLayout.h"
#include "RoboText/RoboTextRunList.h"
#include "RoboText/RoboTextRuns.h"
#include "RoboText/RoboTextSelection.h"
#include "RoboText/RoboTextShape.h"
#include "RoboText/RoboTextTypes.h"
#include "RoboText/RoboTextUnicode.h"
#include "RoboText/RoboTextVersion.h"
#include "RoboText/RoboTextVertical.h"

#endif /* ROBO_TEXT_H */
