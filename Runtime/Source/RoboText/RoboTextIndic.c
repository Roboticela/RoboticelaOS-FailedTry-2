/*
 * Indic shaping (Phase 12.19 §16–§22) — see `RoboTextIndic.h` for the model and the limits.
 *
 * Five pieces, and the first is the data the other four are rules *over*:
 *
 *   **the script rules**   one row per script: its virama, its nukta, and the four positional classes of its
 *                          vowel signs. A script with no row is refused rather than shaped with another
 *                          script's virama.
 *   **clusters**           syllables: a consonant, its viramas, its conjuncts, its matras and its marks — one
 *                          shaping unit with a base inside it.
 *   **masks**              one word per scalar saying which position class it is, so a feature can apply here
 *                          and not there (`half` on the half-forming consonant, `rphf` on the reph, …).
 *   **reordering**         the two moves: shaping order before the features (the reph to the front, a
 *                          pre-base vowel sign after it) and visual order after them (the reph after its base).
 *   **stages**             which features run, in which order, with which reorders between them.
 *
 * The invariant every piece obeys: **the text is never touched**. Clusters are ranges in the text, masks are
 * indexed by source scalar, and a reorder moves *glyphs* — each of which keeps the source range it came from.
 * That is what lets a reordered syllable still answer "which characters are these" in the order they were
 * written (12.19 §7).
 */
#include "RoboText/RoboTextIndic.h"

#include "RoboMem/RoboMem.h"

#include "RoboText/RoboTextUnicode.h"
#include "RoboTextInternal.h"

#include "RoboUnicode/RoboUnicodeProps.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The script rules                                                    */
/* ------------------------------------------------------------------ */

/* Devanagari's vowel signs, by the position they are drawn at. See the header for why they are here. */
static const RoboTextCodePoint RoboDevanagariPreBase[] = { 0x093Fu, 0x094Eu, 0x0955u };
static const RoboTextCodePoint RoboDevanagariBelowBase[] = { 0x0941u, 0x0942u, 0x0943u, 0x0944u,
                                                             0x0962u, 0x0963u };
static const RoboTextCodePoint RoboDevanagariPostBase[] = { 0x093Eu, 0x0940u, 0x0949u, 0x094Au,
                                                            0x094Bu, 0x094Cu };
static const RoboTextCodePoint RoboDevanagariAboveBase[] = { 0x0945u, 0x0946u, 0x0947u, 0x0948u };

/**
 * The rules table.
 *
 * Devanagari's row is the one this build proves; Bengali's and Gujarati's carry their own virama and nukta
 * (so a caller asking `RoboTextIndicIsVirama` gets the right answer for them) but **no** positional classes,
 * and that absence is deliberate: with no pre-base class nothing is reordered, the script shapes in written
 * order, and that is a *named* limitation rather than a silent reorder by another script's vowels. Adding a
 * script is adding its four arrays to this table.
 */
static const RoboTextIndicScriptRules RoboIndicRules[] = {
    { RoboUScriptDevanagari, 0x094Du, 0x093Cu, RoboDevanagariPreBase, 3u, RoboDevanagariBelowBase, 6u,
      RoboDevanagariPostBase, 6u, RoboDevanagariAboveBase, 4u },
    { RoboUScriptBengali, 0x09CDu, 0x09BCu, NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u },
    { RoboUScriptGujarati, 0x0ACDu, 0x0ABCu, NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u },
    { RoboUScriptGurmukhi, 0x0A4Du, 0x0A3Cu, NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u },
    { RoboUScriptOriya, 0x0B4Du, 0x0B3Cu, NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u },
    { RoboUScriptTamil, 0x0BCDu, 0x0BCCu, NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u },
    { RoboUScriptTelugu, 0x0C4Du, 0x0C3Cu, NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u },
    { RoboUScriptKannada, 0x0CCDu, 0x0CBCu, NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u },
    { RoboUScriptMalayalam, 0x0D4Du, 0x0D3Cu, NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u },
    { RoboUScriptSinhala, 0x0DCAu, 0x0DCAu, NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u }
};

const RoboTextIndicScriptRules *RoboTextIndicRulesForScript(RoboTextScript script)
{
    for (size_t index = 0u; index < sizeof(RoboIndicRules) / sizeof(RoboIndicRules[0]); index++) {
        if (RoboIndicRules[index].script == script) {
            return &RoboIndicRules[index];
        }
    }
    return NULL;
}

/** Non-zero when @p cp is in @p list. */
static int IndicInList(const RoboTextCodePoint *list, size_t count, RoboTextCodePoint cp)
{
    if (!list) {
        return 0;
    }
    for (size_t index = 0u; index < count; index++) {
        if (list[index] == cp) {
            return 1;
        }
    }
    return 0;
}

int RoboTextIndicIsVirama(RoboTextScript script, RoboTextCodePoint cp)
{
    const RoboTextIndicScriptRules *rules = RoboTextIndicRulesForScript(script);

    return rules != NULL && rules->virama != 0u && cp == rules->virama;
}

/**
 * Non-zero when @p cp is a consonant.
 *
 * A `Lo` scalar of the script that is **not** a vowel sign, not the virama and not the nukta — which is the
 * definition that works from the tables this build has: the UCD's `Indic_Syllabic_Category` would separate
 * consonants from independent vowels and from consonants with an implicit vowel, and it is **not** in this tree
 * (see the header). What the answer is used for is "can this scalar be a base or carry a virama", and for that
 * question independent vowels and consonants are the same answer.
 */
int RoboTextIndicIsConsonant(RoboTextScript script, RoboTextCodePoint cp)
{
    const RoboTextIndicScriptRules *rules = RoboTextIndicRulesForScript(script);

    if (!rules) {
        return 0;
    }
    if (UnicodeCategory((Uchar32)cp) != RoboUGcLo) {
        return 0;
    }
    if (cp == rules->virama || cp == rules->nukta) {
        return 0;
    }
    if (IndicInList(rules->preBase, rules->preBaseCount, cp) ||
        IndicInList(rules->belowBase, rules->belowBaseCount, cp) ||
        IndicInList(rules->postBase, rules->postBaseCount, cp) ||
        IndicInList(rules->aboveBase, rules->aboveBaseCount, cp)) {
        return 0;
    }
    return RoboTextUnicodeScript(cp) == script;
}

int RoboTextIndicIsAttachment(RoboTextCodePoint cp)
{
    return RoboTextComplexScalarIsMark(cp);
}

/* ------------------------------------------------------------------ */
/* Clusters                                                            */
/* ------------------------------------------------------------------ */

/** What one syllable's scan produced, before it is written to a cluster. */
typedef struct IndicSyllable {
    size_t start;
    size_t end;
    size_t base;
    size_t reph;      /* index of the reph's `ra`, or `(size_t)-1` */
    int hasVirama;
    int hasNukta;
    int hasPreBase;
    int hasPostBase;
    int hasBelowBase;
} IndicSyllable;

/**
 * The scalar index of the syllable's **reph**, or `(size_t)-1`.
 *
 * A reph is the sequence `ra` + virama at the *start of the syllable's shaping sequence* — which is what the
 * initial reorder makes of it — and the definition here is the written one the classifier must apply before
 * anything has moved: a `ra` followed by the script's virama (stepping over a `ZWJ`/`ZWNJ` the author or a
 * font put between them) and then by a consonant. `र्` in `क` `र्` `म` and `र्` in `र्` `व` both answer, and
 * the second is why the initial reorder must be a no-op when the pair is already first.
 *
 * **One implementation, called from both places that need it** (the classifier and the two reorder steps),
 * because a reph the classifier found and a reph the reorder could not locate would be two disagreeing
 * readings of one syllable.
 */
static size_t IndicFindRephScalar(const RoboTextCodePoint *scalars,
                                 size_t start,
                                 size_t end,
                                 RoboTextScript script)
{
    size_t next;

    if (end <= start + 2u || script != RoboUScriptDevanagari || scalars[start] != 0x0930u) {
        return (size_t)-1;
    }
    if (!RoboTextIndicIsVirama(script, scalars[start + 1u])) {
        return (size_t)-1;
    }
    next = start + 2u;
    while (next < end && (scalars[next] == 0x200Cu || scalars[next] == 0x200Du)) {
        next++;
    }
    return next < end && RoboTextIndicIsConsonant(script, scalars[next]) ? start : (size_t)-1;
}

/**
 * Classify one syllable starting at @p start.
 *
 * The scan is one forward pass over the cluster's scalars:
 *
 *   a consonant, then (nukta), then either a virama — which continues the cluster with the next consonant,
 *   stepping over a `ZWJ`/`ZWNJ` that a font may have put there — or a matra or a mark, which attaches to what
 *   came before;
 *
 *   anything else ends the syllable.
 *
 * The **base** is the last consonant that is not followed by a virama: `क` `्` `ष` bases on `ष` (the second
 * consonant is what the conjunct is *built on*), and `क` `र्` `म` bases on `म`. The **reph** is
 * @ref IndicFindRephScalar's answer over the extent the scan found, computed second because the extent is what
 * tells it where the syllable ends.
 */
static void IndicClassifySyllable(const RoboTextCodePoint *scalars,
                                  size_t count,
                                  size_t start,
                                  RoboTextScript script,
                                  IndicSyllable *out)
{
    const RoboTextIndicScriptRules *rules = RoboTextIndicRulesForScript(script);
    size_t at = start;
    size_t lastConsonant = (size_t)-1;
    size_t base = (size_t)-1;
    int expectConsonant = 1;

    memset(out, 0, sizeof(*out));
    out->start = start;
    out->end = start + 1u;
    out->base = 0u;
    out->reph = (size_t)-1;
    if (!rules) {
        return;
    }

    while (at < count) {
        RoboTextCodePoint cp = scalars[at];

        if (RoboTextIndicIsConsonant(script, cp)) {
            /*
             * A consonant joins the syllable only where the syllable is *waiting* for one — at its start, or
             * right after a virama. A consonant after a consonant is a new syllable: `क` `र्` `म` is `क`
             * plus `र्म`, and treating it as one syllable would make the `र्` a conjunct's half form of a
             * cluster it does not belong to. This is the rule the UCD's `Indic_Syllabic_Category` encodes
             * as "an inherent vowel ends the consonant sequence", stated here because that property is not
             * in this build's tables (see the header).
             */
            if (!expectConsonant) {
                break;
            }
            lastConsonant = at;
            expectConsonant = 0;
            /* A base only survives if no virama follows this consonant. */
            if (at + 1u >= count || !RoboTextIndicIsVirama(script, scalars[at + 1u])) {
                base = at;
            }
            out->end = at + 1u;
            at++;
            continue;
        }
        if (cp == rules->nukta) {
            out->hasNukta = 1;
            out->end = at + 1u;
            at++;
            continue;
        }
        if (RoboTextIndicIsVirama(script, cp)) {
            out->hasVirama = 1;
            out->end = at + 1u;
            at++;
            /* A joiner after a virama belongs to the cluster: it asks the font to join (or not). */
            while (at < count && (scalars[at] == 0x200Cu || scalars[at] == 0x200Du)) {
                out->end = at + 1u;
                at++;
            }
            /* The virama is what a following consonant needs in order to join this syllable rather than
             * start its own — and a virama at the end of the text or of the syllable leaves it expecting a
             * consonant that never comes, which is the half form the cluster's own `unsafeToBreak` names. */
            expectConsonant = 1;
            continue;
        }
        if (IndicInList(rules->preBase, rules->preBaseCount, cp)) {
            if (at == start) {
                break;
            }
            out->hasPreBase = 1;
            out->end = at + 1u;
            at++;
            continue;
        }
        if (IndicInList(rules->belowBase, rules->belowBaseCount, cp) ||
            IndicInList(rules->postBase, rules->postBaseCount, cp) ||
            IndicInList(rules->aboveBase, rules->aboveBaseCount, cp) ||
            RoboTextComplexScalarIsMark(cp)) {
            /* A mark attaches to the syllable; at the very start it is a mark cluster of its own. */
            if (at == start) {
                break;
            }
            out->hasBelowBase |= IndicInList(rules->belowBase, rules->belowBaseCount, cp);
            out->hasPostBase |= IndicInList(rules->postBase, rules->postBaseCount, cp);
            out->end = at + 1u;
            at++;
            continue;
        }
        break;
    }

    if (base == (size_t)-1) {
        /*
         * No consonant survived as a base (a lone half consonant, or a syllable of marks): the base is the
         * last consonant seen, and the cluster's first scalar when there is none — a mark cluster bases on
         * itself rather than on nothing.
         */
        base = lastConsonant != (size_t)-1 ? lastConsonant : start;
    }
    out->reph = IndicFindRephScalar(scalars, out->start, out->end, script);
    if (out->reph == base) {
        out->reph = (size_t)-1;
    }
    out->base = base;
}

Result RoboTextIndicClusters(RoboTextView view,
                             RoboTextRange range,
                             RoboTextComplexCluster *clusters,
                             size_t capacity,
                             size_t *outCount,
                             RoboTextDiag *diag)
{
    RoboTextCodePoint *scalars = NULL;
    size_t count = 0u;
    size_t produced = 0u;
    Result status;

    if (!outCount) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "a cluster count output is required");
    }
    *outCount = 0u;
    if (RoboTextRangeLength(range) == 0u) {
        return RoboOk;
    }
    if (!clusters && capacity != 0u) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "a capacity without an array is not a request");
    }
    status = RoboTextComplexDecodeRange(view, range, NULL, 0u, &count, diag);
    if (status != RoboOk) {
        return status;
    }
    scalars = (RoboTextCodePoint *)RoboCalloc(count, sizeof(*scalars));
    if (!scalars) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboOutOfMemory,
                               "the syllable pass could not decode the range");
    }
    status = RoboTextComplexDecodeRange(view, range, scalars, count, &count, diag);

    for (size_t at = 0u; status == RoboOk && at < count;) {
        RoboTextScript script = (RoboTextScript)RoboTextUnicodeScript(scalars[at]);
        const RoboTextIndicScriptRules *rules = RoboTextIndicRulesForScript(script);
        IndicSyllable syllable;

        if (!rules || !RoboTextIndicIsConsonant(script, scalars[at])) {
            /* Not a syllable start this build knows: one scalar (plus nothing) is its own cluster. */
            memset(&syllable, 0, sizeof(syllable));
            syllable.start = at;
            syllable.end = at + 1u;
            syllable.base = at;
            syllable.reph = (size_t)-1;
            at++;
        } else {
            IndicClassifySyllable(scalars, count, at, script, &syllable);
            if (syllable.end <= syllable.start) {
                syllable.end = syllable.start + 1u;
            }
            at = syllable.end;
        }

        if (produced < capacity) {
            RoboTextComplexCluster *cluster = &clusters[produced];

            memset(cluster, 0, sizeof(*cluster));
            cluster->source = RoboTextRangeMake(range.start + syllable.start, range.start + syllable.end);
            cluster->strategy = RoboTextComplexStrategyIndic;
            cluster->baseIndex = syllable.base - syllable.start;
            cluster->hasReph = syllable.reph != (size_t)-1 ? 1 : 0;
            cluster->hasPreBase = syllable.hasPreBase;
            cluster->hasPostBase = syllable.hasPostBase;
            cluster->hasBelowBase = syllable.hasBelowBase;
            cluster->hasVirama = syllable.hasVirama;
            cluster->hasNukta = syllable.hasNukta;
            if (!rules || !RoboTextIndicIsConsonant(script, scalars[syllable.start])) {
                cluster->kind = RoboTextComplexScalarIsMark(scalars[syllable.start])
                                    ? RoboTextComplexClusterMarks
                                    : (UnicodeIsDefaultIgnorable((Uchar32)scalars[syllable.start])
                                           ? RoboTextComplexClusterControl
                                           : RoboTextComplexClusterSimple);
            } else {
                cluster->kind = RoboTextComplexClusterSyllable;
            }
            /*
             * A syllable whose last scalar is the virama attaches **forward**: the half form it produces
             * belongs to the consonant that follows, even when that consonant begins another cluster (a space
             * between them is not a break the author can make). This is §28's forward half.
             */
            if (cluster->kind == RoboTextComplexClusterSyllable &&
                (RoboTextIndicIsVirama(script, scalars[syllable.end - 1u]) ||
                 cluster->hasReph)) {
                cluster->unsafeToBreak = 1;
            }
        }
        produced++;
    }

    RoboFree(scalars);
    if (status != RoboOk) {
        return status;
    }
    *outCount = produced;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Masks                                                               */
/* ------------------------------------------------------------------ */

Result RoboTextIndicMasksBuild(RoboTextView view,
                               RoboTextRange range,
                               uint32_t *masks,
                               size_t capacity,
                               size_t *outCount,
                               RoboTextDiag *diag)
{
    RoboTextCodePoint *scalars = NULL;
    RoboTextComplexCluster *clusters = NULL;
    size_t count = 0u;
    size_t clusterCount = 0u;
    Result status;

    if (!masks || !outCount) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "a mask array and a count are required");
    }
    *outCount = 0u;
    if (RoboTextRangeLength(range) == 0u) {
        return RoboOk;
    }
    if (capacity < RoboTextRangeLength(range)) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboNotSupported,
                               "the caller's array cannot hold the range's masks");
    }
    status = RoboTextComplexDecodeRange(view, range, NULL, 0u, &count, diag);
    if (status != RoboOk) {
        return status;
    }
    scalars = (RoboTextCodePoint *)RoboCalloc(count, sizeof(*scalars));
    clusters = (RoboTextComplexCluster *)RoboCalloc(count, sizeof(*clusters));
    if (!scalars || !clusters) {
        RoboFree(scalars);
        RoboFree(clusters);
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboOutOfMemory,
                               "the mask pass could not hold the range");
    }
    status = RoboTextComplexDecodeRange(view, range, scalars, count, &count, diag);
    if (status == RoboOk) {
        status = RoboTextIndicClusters(view, range, clusters, count, &clusterCount, diag);
    }

    if (status == RoboOk) {
        memset(masks, 0, RoboTextRangeLength(range) * sizeof(*masks));
        for (size_t index = 0u; index < clusterCount; index++) {
            const RoboTextComplexCluster *cluster = &clusters[index];
            RoboTextScript script =
                (RoboTextScript)RoboTextUnicodeScript(scalars[cluster->source.start - range.start]);
            size_t first = cluster->source.start - range.start;
            size_t last = cluster->source.end - range.start;
            size_t base = first + cluster->baseIndex;
            size_t reph = cluster->hasReph ? IndicFindRephScalar(scalars, first, last, script)
                                            : (size_t)-1;
            size_t consonants = 0u;

            for (size_t at = first; at < last; at++) {
                if (RoboTextIndicIsConsonant(script, scalars[at])) {
                    consonants++;
                }
            }
            for (size_t at = first; at < last; at++) {
                RoboTextCodePoint cp = scalars[at];
                uint32_t mask = 0u;

                if (RoboTextIndicIsConsonant(script, cp)) {
                    mask |= (uint32_t)RoboTextIndicMaskConsonant;
                    if (at == reph) {
                        /*
                         * The reph carries `rphf`'s bit on **the `ra` itself**, because that is the scalar the
                         * glyph the lookup is tried at came from: the engine asks a position's mask, the
                         * position's mask is the mask of the glyph's source scalar, and `rphf`'s lookup
                         * matches at the first glyph of the pair. Giving the bit to the syllable's first
                         * scalar instead would be right in every syllable whose reph is first and wrong in
                         * every other, which is the difference this reads the scalars to avoid.
                         */
                        mask |= (uint32_t)RoboTextIndicMaskReph;
                    }
                    if (at + 1u < last && RoboTextIndicIsVirama(script, scalars[at + 1u])) {
                        /*
                         * A consonant that a virama follows is a *half* form candidate — the left element of
                         * a conjunct — and never the base. There is deliberately no `at != base` guard: a
                         * base that a virama follows is the syllable the classifier **fell back** on (nothing
                         * followed the virama), and that is precisely the trailing `क` `्` whose half form is
                         * the whole of what the syllable has to produce.
                         */
                        mask |= (uint32_t)RoboTextIndicMaskHalf;
                    }
                    if (at >= base) {
                        mask |= (uint32_t)RoboTextIndicMaskBelow;
                    }
                    if (at > base) {
                        mask |= (uint32_t)RoboTextIndicMaskPost;
                    }
                    if (consonants > 1u && at == first && at != base) {
                        mask |= (uint32_t)RoboTextIndicMaskPre;
                    }
                    if (consonants > 1u) {
                        /*
                         * Every consonant of a multi-consonant syllable, because the engine asks a lookup's
                         * mask at the position its match **starts** — and a conjunct lookup starts at the
                         * sequence's leftmost consonant, not at the base. Marking only the consonants after
                         * the first would leave `half` + `cjct` unable to match the half form it just made,
                         * which is the shape `text.indic.conjunct` pins.
                         */
                        mask |= (uint32_t)RoboTextIndicMaskConjunct;
                    }
                }
                masks[at] |= mask;
            }
        }
        *outCount = RoboTextRangeLength(range);
    }

    RoboFree(clusters);
    RoboFree(scalars);
    return status;
}

/* ------------------------------------------------------------------ */
/* Reordering                                                          */
/* ------------------------------------------------------------------ */

/**
 * Move @p count glyphs starting at @p from so that they sit immediately before buffer index @p before.
 *
 * Both directions, and it is the buffer's own primitive twice rather than a hand-rolled shift: copy the span
 * out, delete it, insert it at the (corrected) destination. A destination *inside* the moved span is a no-op,
 * because such a move has no meaning and the alternative would be to move glyphs past themselves.
 *
 * The glyphs are moved **whole** — id, source, cluster, level and every flag — so provenance survives the
 * move by construction and no reorder can invent a source range (12.19 §7).
 */
static Result IndicMoveGlyphs(RoboTextShapingBuffer *buffer, size_t from, size_t count, size_t before)
{
    RoboTextShapingGlyph moved[4];

    if (count == 0u || count > sizeof(moved) / sizeof(moved[0])) {
        return RoboOk;
    }
    if (before == from || (before > from && before < from + count)) {
        /* Already there, or a destination inside the span the move would carry: both are no-ops rather
         * than errors, because a strategy that asks for one has asked for nothing. */
        return RoboOk;
    }
    for (size_t index = 0u; index < count; index++) {
        const RoboTextShapingGlyph *glyph = RoboTextShapingBufferAt(buffer, from + index);

        if (!glyph) {
            return RoboOk;
        }
        moved[index] = *glyph;
    }
    if (RoboTextShapingBufferReplace(buffer, from, count, NULL, 0u) != RoboOk) {
        return RoboOutOfMemory;
    }
    if (before > from) {
        before -= count;
    }
    return RoboTextShapingBufferReplace(buffer, before, 0u, moved, count);
}

/** The buffer index of the glyph whose source **covers** scalar @p scalar, or `(size_t)-1`. */
static size_t IndicGlyphCovering(const RoboTextShapingBuffer *buffer, RoboTextScalarIndex scalar)
{
    for (size_t index = 0u; index < RoboTextShapingBufferCount(buffer); index++) {
        const RoboTextShapingGlyph *glyph = RoboTextShapingBufferAt(buffer, index);

        if (!glyph) {
            continue;
        }
        if (glyph->source.start <= scalar && scalar < glyph->source.end) {
            return index;
        }
    }
    return (size_t)-1;
}

/**
 * The scalar index of the syllable's pre-base vowel sign, or `(size_t)-1`.
 *
 * Read from the script's own rules rather than from anything in a glyph, because this is the *written* text and
 * the point of the move is that the glyph has not been made yet.
 */
static size_t IndicFindPreBaseScalar(const RoboTextCodePoint *scalars,
                                     size_t start,
                                     size_t end,
                                     RoboTextScript script)
{
    const RoboTextIndicScriptRules *rules = RoboTextIndicRulesForScript(script);

    if (!rules) {
        return (size_t)-1;
    }
    for (size_t at = start; at < end; at++) {
        if (IndicInList(rules->preBase, rules->preBaseCount, scalars[at])) {
            return at;
        }
    }
    return (size_t)-1;
}

Result RoboTextIndicReorderInitial(RoboTextShapingBuffer *buffer,
                                   RoboTextView view,
                                   RoboTextRange range,
                                   RoboTextDiag *diag)
{
    RoboTextComplexCluster *clusters = NULL;
    RoboTextCodePoint *scalars = NULL;
    size_t count = 0u;
    size_t scalarCount = 0u;
    Result status;

    if (!buffer) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "a shaping buffer is required");
    }
    if (RoboTextRangeLength(range) == 0u) {
        return RoboOk;
    }
    clusters = (RoboTextComplexCluster *)RoboCalloc(RoboTextRangeLength(range), sizeof(*clusters));
    if (!clusters) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboOutOfMemory,
                               "the initial reorder could not hold the range's clusters");
    }
    status = RoboTextIndicClusters(view, range, clusters, RoboTextRangeLength(range), &count, diag);
    if (status == RoboOk) {
        /* The written scalars, decoded **once** for the whole range: the moves are stated in the text's order
         * and a per-cluster decode would be the same bytes read once per syllable. */
        status = RoboTextComplexDecodeRange(view, range, NULL, 0u, &scalarCount, diag);
        if (status == RoboOk && scalarCount != 0u) {
            scalars = (RoboTextCodePoint *)RoboCalloc(scalarCount, sizeof(*scalars));
            if (!scalars) {
                status = RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboOutOfMemory,
                                         "the initial reorder could not hold the range's scalars");
            } else {
                status = RoboTextComplexDecodeRange(view, range, scalars, scalarCount, &scalarCount, diag);
            }
        }
    }

    for (size_t index = 0u; status == RoboOk && scalars && index < count; index++) {
        const RoboTextComplexCluster *cluster = &clusters[index];
        size_t first = cluster->source.start - range.start;
        size_t last = cluster->source.end - range.start;
        RoboTextScript script;
        size_t preBase;
        size_t baseGlyph;
        size_t signGlyph;

        if (cluster->kind != RoboTextComplexClusterSyllable || !cluster->hasPreBase) {
            continue;
        }
        script = (RoboTextScript)RoboTextUnicodeScript(scalars[first]);
        preBase = IndicFindPreBaseScalar(scalars, first, last, script);
        if (preBase == (size_t)-1) {
            continue;
        }
        /*
         * Written order is not shaping order for exactly one element here: the vowel sign is written after
         * the consonant it belongs to and drawn before it, so it moves to immediately **before the base**.
         * Before the base and not before the syllable, which is what makes `र्मि` come out as reph, sign,
         * base rather than sign, reph, base — the reph is a part of the base's own cluster.
         *
         * The reph's own half of this step needs no code, and that is a property of the classifier rather
         * than an omission: @ref IndicFindRephScalar only finds a reph **at the syllable's start**, so the
         * pair `rphf` has to match is already first and a move would be a no-op. A reph found mid-syllable
         * would have to be moved here, and that is a change to one detector rather than to this loop.
         */
        baseGlyph = IndicGlyphCovering(buffer, (RoboTextScalarIndex)(cluster->source.start +
                                                                    cluster->baseIndex));
        signGlyph = IndicGlyphCovering(buffer,
                                       (RoboTextScalarIndex)(range.start + preBase));
        if (baseGlyph == (size_t)-1 || signGlyph == (size_t)-1 || signGlyph == baseGlyph) {
            continue;
        }
        status = IndicMoveGlyphs(buffer, signGlyph, 1u, baseGlyph);
    }

    RoboFree(scalars);
    RoboFree(clusters);
    return status;
}

Result RoboTextIndicReorderFinal(RoboTextShapingBuffer *buffer,
                                 RoboTextView view,
                                 RoboTextRange range,
                                 RoboTextDiag *diag)
{
    RoboTextComplexCluster *clusters = NULL;
    size_t count = 0u;
    Result status;

    if (!buffer) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "a shaping buffer is required");
    }
    if (RoboTextRangeLength(range) == 0u) {
        return RoboOk;
    }
    clusters = (RoboTextComplexCluster *)RoboCalloc(RoboTextRangeLength(range), sizeof(*clusters));
    if (!clusters) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboOutOfMemory,
                               "the final reorder could not hold the range's clusters");
    }
    status = RoboTextIndicClusters(view, range, clusters, RoboTextRangeLength(range), &count, diag);

    for (size_t index = 0u; status == RoboOk && index < count; index++) {
        const RoboTextComplexCluster *cluster = &clusters[index];
        RoboTextScalarIndex baseScalar;
        size_t rephGlyph;
        size_t lastGlyph = (size_t)-1;

        if (cluster->kind != RoboTextComplexClusterSyllable || !cluster->hasReph) {
            continue;
        }
        baseScalar = (RoboTextScalarIndex)(cluster->source.start + cluster->baseIndex);
        /*
         * The reph is `ra` + virama at the syllable's start, and by now `rphf` has had its chance at it: the
         * glyph covering the `ra` is the reph — one glyph, whose source is the union of the pair it replaced,
         * or still two glyphs if the font has no `rphf`, in which case the `ra` moves and the virama follows it
         * in the next step of the same scan. It must be placed after the **base**, and "after the base" is the
         * last glyph of the syllable at or before the base scalar rather than the base scalar's own glyph:
         * a conjunct's base may be a glyph that also covers consonants the reph does not follow.
         */
        rephGlyph = IndicGlyphCovering(buffer, cluster->source.start);
        if (rephGlyph == (size_t)-1) {
            continue;
        }
        for (size_t slot = 0u; slot < RoboTextShapingBufferCount(buffer); slot++) {
            const RoboTextShapingGlyph *glyph = RoboTextShapingBufferAt(buffer, slot);

            if (!glyph || glyph->source.start < cluster->source.start ||
                glyph->source.start > baseScalar) {
                continue;
            }
            if (slot == rephGlyph) {
                continue;
            }
            lastGlyph = slot;
        }
        if (lastGlyph == (size_t)-1 || rephGlyph > lastGlyph) {
            continue;
        }
        /* The reph moves to immediately after the base. */
        status = IndicMoveGlyphs(buffer, rephGlyph, 1u, lastGlyph + 1u);
        if (status != RoboOk) {
            break;
        }
    }

    RoboFree(clusters);
    return status;
}

/* ------------------------------------------------------------------ */
/* The stages                                                          */
/* ------------------------------------------------------------------ */

static RoboTextComplexStageFeature IndicFeature(const char *tag, uint32_t mask)
{
    RoboTextComplexStageFeature feature;

    memset(&feature, 0, sizeof(feature));
    for (size_t index = 0u; index < 4u && tag[index] != '\0'; index++) {
        feature.tag[index] = tag[index];
    }
    feature.mask = mask;
    return feature;
}

Result RoboTextIndicStages(const struct RoboTextComplexPlan *plan,
                           RoboTextComplexStage *stages,
                           size_t capacity,
                           size_t *outCount,
                           RoboTextDiag *diag)
{
    (void)plan;
    if (!stages || !outCount) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "a stage array and a count are required");
    }
    *outCount = 0u;
    if (capacity < 3u) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboNotSupported,
                               "the Indic strategy needs three stages");
    }

    /*
     * Stage 1 — composition and locale forms, with the **initial reorder before it**.
     *
     * Before, because the reorder is expressed in scalars and the buffer holds one glyph per scalar until a
     * substitution changes that (`RoboTextComplexStage::reorderBefore` states the rule). A consequence worth
     * naming: `ccmp` sees the reordered sequence rather than the written one, which is harmless for the
     * composition it does (base and mark, adjacent either way) and is *not* what HarfBuzz does — it reorders
     * between the first and second stage. A different choice here would move the moves into glyph space,
     * which is where they would be after `ccmp` had composed anything.
     */
    memset(&stages[0], 0, sizeof(stages[0]));
    stages[0].name = "indic.common";
    stages[0].features[0] = IndicFeature("ccmp", RoboTextComplexMaskAll);
    stages[0].features[1] = IndicFeature("locl", RoboTextComplexMaskAll);
    stages[0].featureCount = 2u;
    stages[0].reorderBefore = RoboTextComplexReorderInitial;
    stages[0].reorderAfter = RoboTextComplexReorderNone;

    /*
     * Stage 2 — the substitution stage, and the heart of Indic shaping: the features whose *positions* decide
     * what a conjunct becomes, each with the mask of the class it applies at. The unmasked ones apply
     * throughout the syllable, which is what the phase's §20 asks: the font's own features decide, and this
     * plan only says *where*.
     */
    memset(&stages[1], 0, sizeof(stages[1]));
    stages[1].name = "indic.substitute";
    stages[1].features[0] = IndicFeature("nukt", RoboTextComplexMaskAll);
    stages[1].features[1] = IndicFeature("akhn", RoboTextComplexMaskAll);
    stages[1].features[2] = IndicFeature("rphf", RoboTextIndicMaskReph);
    stages[1].features[3] = IndicFeature("rkrf", RoboTextComplexMaskAll);
    stages[1].features[4] = IndicFeature("vatu", RoboTextComplexMaskAll);
    stages[1].features[5] = IndicFeature("pref", RoboTextIndicMaskPre);
    stages[1].features[6] = IndicFeature("blwf", RoboTextIndicMaskBelow);
    stages[1].features[7] = IndicFeature("abvf", RoboTextComplexMaskAll);
    stages[1].features[8] = IndicFeature("half", RoboTextIndicMaskHalf);
    stages[1].features[9] = IndicFeature("pstf", RoboTextIndicMaskPost);
    stages[1].features[10] = IndicFeature("cjct", RoboTextIndicMaskConjunct);
    stages[1].featureCount = 11u;
    stages[1].reorderBefore = RoboTextComplexReorderNone;
    stages[1].reorderAfter = RoboTextComplexReorderFinal;

    /*
     * Stage 3 — the presentation features, on glyphs that are already in visual order: they shape what the
     * substitution stage decided rather than the sequence it decided it in, which is why they run last and
     * unmasked.
     */
    memset(&stages[2], 0, sizeof(stages[2]));
    stages[2].name = "indic.presentation";
    stages[2].features[0] = IndicFeature("pres", RoboTextComplexMaskAll);
    stages[2].features[1] = IndicFeature("abvs", RoboTextComplexMaskAll);
    stages[2].features[2] = IndicFeature("blws", RoboTextComplexMaskAll);
    stages[2].features[3] = IndicFeature("psts", RoboTextComplexMaskAll);
    stages[2].features[4] = IndicFeature("haln", RoboTextComplexMaskAll);
    stages[2].features[5] = IndicFeature("calt", RoboTextComplexMaskAll);
    stages[2].featureCount = 6u;
    stages[2].reorderBefore = RoboTextComplexReorderNone;
    stages[2].reorderAfter = RoboTextComplexReorderNone;

    *outCount = 3u;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

Result RoboTextIndicClustersWrite(const RoboTextComplexCluster *clusters,
                                  size_t count,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength)
{
    return RoboTextComplexClustersWrite(clusters, count, buffer, capacity, outLength);
}
