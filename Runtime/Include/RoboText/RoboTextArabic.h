/*
 * Arabic shaping in RoboText (Phase 12.19 §8–§15) — joining, contextual forms, clusters, features.
 *
 * ## The model, in one paragraph
 *
 * Arabic letters *connect*. Whether a letter takes its isolated, initial, medial or final shape is decided
 * by the letters beside it, and the font expresses those four shapes as four features (`isol`, `init`,
 * `medi`, `fina`) whose lookups **substitute one glyph for one glyph**. So shaping Arabic is: read each
 * scalar's `Joining_Type` (RoboUnicode's, not a table here — §9), resolve the four contextual forms from the
 * neighbours, and then ask the font's features to apply *at the positions each form belongs to*. Nothing
 * else: no glyph id is written here, no substitution is implemented here, and the ligatures (`lam` + `alef`
 * above all) come from the font's `rlig` like any other ligature.
 *
 * ## What this file owns, and what it does not
 *
 * Owns: the **forms** (a scalar's contextual shape), the **position masks** that carry them into the engine,
 * the **clusters** (one joining sequence, which is what must not be split by a fallback or a line break), and
 * the **stages** (which features, in which order, at which positions).
 *
 * Does not own: joining *types* (RoboUnicode's `Joining_Type`), the direction (bidi's — §15), the
 * substitution (GSUB's), the positioning of marks and cursive connections (GPOS's), or any text ordering.
 *
 * ## Two rules that are easy to get wrong, and are therefore stated
 *
 *   **A transparent scalar is invisible to joining.** A combining mark between two letters does not break
 *   their connection: the neighbour walk skips it. It also takes **no form** of its own — `RoboTextArabicFormNone`
 *   — because a mark's shape is not a connecting shape; giving it a form would ask the font for `medi` on a
 *   diacritic.
 *
 *   **A right-joining letter is not a "final" letter by accident.** `ALEF`, `DAL`, `REH`, `WAW` accept a
 *   connection from the letter before them and **never** give one to the letter after, which is why `ALEF` is
 *   `Final` in `با` and why the letter after it is `Initial` or `Isolated` rather than `Medial`. The test
 *   matrix in `Tests/Text/SuiteRoboTextArabic.c` pins all four shapes plus that asymmetry.
 */
#ifndef ROBO_TEXT_ARABIC_H
#define ROBO_TEXT_ARABIC_H

#include "RoboBase/RoboError.h"
#include "RoboText/RoboTextComplex.h"
#include "RoboText/RoboTextDiagnostics.h"
#include "RoboText/RoboTextTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The contextual form one scalar takes — the four OpenType states plus "not a joining character".
 *
 * `None` is not a fifth shape: it is the answer for a scalar that has no contextual shape at all (a mark, a
 * space, a digit, `HAMZA`). It maps to no mask bit, so no form feature is applied at that position.
 */
typedef enum RoboTextArabicForm {
    /** The scalar is not a joining character: a mark, a control, punctuation, a non-joining letter. */
    RoboTextArabicFormNone = 0,
    /** Not connected to either neighbour: `isol`. */
    RoboTextArabicFormIsolated = 1,
    /** Connected to the letter after it only: `init`. */
    RoboTextArabicFormInitial = 2,
    /** Connected on both sides: `medi`. */
    RoboTextArabicFormMedial = 3,
    /** Connected to the letter before it only: `fina`. */
    RoboTextArabicFormFinal = 4
} RoboTextArabicForm;

/** `"isolated"`, `"initial"`, `"medial"`, `"final"`, `"none"`. Never NULL. */
const char *RoboTextArabicFormName(RoboTextArabicForm form);

/** The OpenType feature tag whose lookup produces @p form: `"isol"`, `"init"`, `"medi"`, `"fina"`. */
const char *RoboTextArabicFormFeature(RoboTextArabicForm form);

/**
 * The mask bit a form's feature is applied at.
 *
 * One bit per form, and the four are the whole vocabulary of the Arabic strategy's positional features: a
 * scalar's mask word names its form, and a feature whose `RoboTextComplexStageFeature::mask` names the same
 * bit runs there and nowhere else. Four bits of a `uint32_t` are plenty, and the low four are chosen so a
 * dump reads `1` for isolated and `8` for final.
 */
typedef enum RoboTextArabicMask {
    RoboTextArabicMaskIsolated = 1u << 0,
    RoboTextArabicMaskInitial = 1u << 1,
    RoboTextArabicMaskMedial = 1u << 2,
    RoboTextArabicMaskFinal = 1u << 3
} RoboTextArabicMask;

/** The mask bit for @p form, or 0 for `RoboTextArabicFormNone`. */
uint32_t RoboTextArabicFormMask(RoboTextArabicForm form);

/** Non-zero when @p form is one of the four connecting shapes. */
int RoboTextArabicFormIsShaped(RoboTextArabicForm form);

/* ------------------------------------------------------------------ */
/* Joining resolution                                                  */
/* ------------------------------------------------------------------ */

/**
 * Resolve one contextual form per scalar of @p range.
 *
 * `out[index]` is the form of the scalar at `range.start + index`, so the array is parallel to the scalars
 * and a caller reads it with the index it already has. The neighbour walk reads `Joining_Type` from RoboUnicode
 * and steps **over** transparent scalars, so a letter with two diacritics between it and its neighbour still
 * connects to that neighbour.
 *
 * Deterministic, allocation-free (the caller's array is the only storage) and total: every scalar answers,
 * and a scalar outside the Arabic script simply answers `None` or `Isolated` — the rule does not care what
 * script a scalar belongs to, only what its joining type is, which is why a Syriac letter would answer here
 * if the table carried its block.
 *
 * @param capacity how many entries @p out holds; a range longer than that is refused rather than truncated,
 *                 because half a form array is a plan whose second half is silently wrong.
 * @return RoboOk; RoboInvalidArgument for a NULL out/count pair or a range outside @p view; RoboNotSupported
 *         when @p capacity is smaller than the range.
 */
Result RoboTextArabicJoiningResolve(RoboTextView view,
                                    RoboTextRange range,
                                    RoboTextArabicForm *out,
                                    size_t capacity,
                                    size_t *outCount,
                                    RoboTextDiag *diag);

/**
 * The position masks the Arabic substitution stage runs with: one `uint32_t` per scalar, holding the mask bit
 * of that scalar's form.
 *
 * This is the bridge between the model above and the engine: the engine asks "does this lookup apply at this
 * position", the position's answer is this word, and the lookup's answer is its own mask from the stage. A
 * scalar with no form answers 0, which matches no feature and therefore runs nothing.
 *
 * @param masks the caller's array, parallel to the scalars of @p range.
 */
Result RoboTextArabicMasksBuild(RoboTextView view,
                                RoboTextRange range,
                                uint32_t *masks,
                                size_t capacity,
                                size_t *outCount,
                                RoboTextDiag *diag);

/**
 * The Arabic clusters of @p range: one per **joining sequence**, with a mark belonging to the letter it
 * follows.
 *
 * A joining sequence is what a fallback must not split (12.19 §25) and what a line breaker must not break
 * (12.19 §28), so it is the unit here: `كتب` is one cluster, `كتب،` is two (the comma does not join), and a
 * `ZWNJ` between two letters ends the first and starts a second, which is exactly what that character is for.
 * Per-scalar caret movement stays where it was — 12.14's glyph clusters — because a cluster here answers
 * "what shapes together", not "what the user selects".
 */
Result RoboTextArabicClusters(RoboTextView view,
                              RoboTextRange range,
                              RoboTextComplexCluster *clusters,
                              size_t capacity,
                              size_t *outCount,
                              RoboTextDiag *diag);

/* ------------------------------------------------------------------ */
/* The stages                                                          */
/* ------------------------------------------------------------------ */

/**
 * The Arabic strategy's stages, in order: the common features, the four contextual forms (masked), and the
 * ligature/contextual features.
 *
 * `RoboTextComplexPlanResolve` calls this and then applies the caller's own settings, so a caller's
 * `"fina" 0` removes that feature from its stage while leaving the other three alone. The feature list is the
 * phase's §11, filtered to the features Arabic actually uses:
 *
 *   `ccmp`, `locl`            composition and locale forms, at every position;
 *   `isol`, `init`, `medi`, `fina`, `fin2`, `fin3`, `med2`
 *                             the contextual forms at the positions their mask names (`fin2`/`fin3`/`med2` are
 *                             the Syriac-valued alternates and are harmless for a font that lists none);
 *   `rlig`, `calt`, `liga`, `clig`, `dlig`
 *                             required ligatures (`lam` + `alef`), contextual alternates and the ligature
 *                             family, at every position — and the caller's ligature policy has already
 *                             spoken for `liga`/`clig`/`dlig`/`rlig` by the time this runs.
 */
Result RoboTextArabicStages(const struct RoboTextComplexPlan *plan,
                            RoboTextComplexStage *stages,
                            size_t capacity,
                            size_t *outCount,
                            RoboTextDiag *diag);

/** One line per scalar: index, form and mask (`"#0 U+0643 form=initial mask=0x2"`). Complete-or-absent. */
Result RoboTextArabicJoiningWrite(RoboTextView view,
                                  RoboTextRange range,
                                  const RoboTextArabicForm *forms,
                                  size_t count,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEXT_ARABIC_H */
