/*
 * RoboLayout's resolved font request (Phase 12.17) — see RoboLayoutFontRequest.h.
 *
 * Three jobs, in the order the header argues them: the family entries (a bounded value type), the
 * family-list parser (commas, quotes, keyword classification through RoboFont's own vocabulary), and
 * the CSS adapter (five properties, four of them delegated to the measurer's scalar resolver so the
 * two records cannot disagree).
 *
 * Nothing here loads a font, maps a scalar to a glyph, chooses a fallback or shapes. The family list
 * leaves this file as **data** — ordered, classified, pointer-free — for a selector to read.
 */

#include "RoboLayout/RoboLayoutFontRequest.h"

#include "RoboCSS/RoboCSSComputedValue.h"
#include "RoboCSS/RoboCSSTypes.h"
#include "RoboFont/RoboFontGenericFamily.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* One family entry                                                    */
/* ------------------------------------------------------------------ */

void LayoutFontFamilyInit(LayoutFontFamily *family)
{
    if (!family) {
        return;
    }
    memset(family, 0, sizeof(*family));
    family->kind = LayoutFontFamilyNamed;
    family->generic = RoboFontGenericNone;
}

int LayoutFontFamilyEqual(const LayoutFontFamily *a, const LayoutFontFamily *b)
{
    if (!a || !b) {
        return !a && !b;
    }
    if (a->kind != b->kind) {
        return 0;
    }
    if (a->kind == LayoutFontFamilyGeneric) {
        return a->generic == b->generic;
    }
    return CssSliceEqualsAsciiCi(CssSliceFromText(a->name),
                                 b->name);
}

const char *LayoutFontFamilyText(const LayoutFontFamily *family)
{
    if (!family) {
        return "none";
    }
    if (family->kind == LayoutFontFamilyGeneric) {
        return FontGenericFamilyName(family->generic);
    }
    return family->name;
}

/* ------------------------------------------------------------------ */
/* The family list                                                     */
/* ------------------------------------------------------------------ */

void LayoutFontFamilyListInit(LayoutFontFamilyList *list)
{
    uint32_t index;

    if (!list) {
        return;
    }
    for (index = 0u; index < RoboLayoutFontMaxFamilies; index++) {
        LayoutFontFamilyInit(&list->entries[index]);
    }
    list->count = 0u;
}

Result LayoutFontFamilyListAppend(LayoutFontFamilyList *list,
                                  const LayoutFontFamily *family)
{
    if (!list || !family) {
        return RoboInvalidArgument;
    }
    if (list->count >= RoboLayoutFontMaxFamilies) {
        return RoboAlreadyExists;
    }
    list->entries[list->count] = *family;
    list->count += 1u;
    return RoboOk;
}

int LayoutFontFamilyListIsEmpty(const LayoutFontFamilyList *list)
{
    return !list || list->count == 0u;
}

int LayoutFontFamilyListEqual(const LayoutFontFamilyList *a,
                              const LayoutFontFamilyList *b)
{
    uint32_t index;

    if (!a || !b) {
        return !a && !b;
    }
    if (a->count != b->count) {
        return 0;
    }
    for (index = 0u; index < a->count; index++) {
        if (!LayoutFontFamilyEqual(&a->entries[index], &b->entries[index])) {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Parsing                                                             */
/* ------------------------------------------------------------------ */

/** Copy @p bytes into @p family, classifying a generic by RoboFont's vocabulary. */
static void LayoutFontFamilySet(LayoutFontFamily *family,
                                const char *bytes,
                                size_t length)
{
    FontGenericFamily generic;

    LayoutFontFamilyInit(family);
    if (length >= RoboLayoutFontMaxFamilyBytes) {
        length = RoboLayoutFontMaxFamilyBytes - 1u;
    }
    memcpy(family->name, bytes, length);
    family->name[length] = '\0';

    /*
     * The keyword test is RoboFont's own reader, so the vocabulary this build accepts is the one the
     * font system already uses and a new generic is a row there rather than an edit here.
     */
    if (FontGenericFamilyFromName(family->name, length, &generic) == RoboOk &&
        generic != RoboFontGenericNone) {
        family->kind = LayoutFontFamilyGeneric;
        family->generic = generic;
        family->name[0] = '\0';
        return;
    }
    family->kind = LayoutFontFamilyNamed;
    family->generic = RoboFontGenericNone;
}

/** Trim ASCII whitespace from both ends of `[first, end)`. */
static void LayoutTrim(const char **first, const char **end)
{
    while (*first < *end &&
           (**first == ' ' || **first == '\t' || **first == '\n' || **first == '\r')) {
        *first += 1;
    }
    while (*end > *first) {
        char c = *(*end - 1);

        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            break;
        }
        *end -= 1;
    }
}

Result LayoutFontFamilyListParse(const char *text,
                                 size_t length,
                                 LayoutFontFamilyList *out)
{
    size_t cursor = 0u;

    if (!out) {
        return RoboInvalidArgument;
    }
    LayoutFontFamilyListInit(out);
    if (!text) {
        return RoboOk;
    }
    /* A zero length means "the text is NUL-terminated", matching the convention RoboFont's own
     * name-taking doors use; an empty string still yields an empty list. */
    if (length == 0u) {
        length = strlen(text);
    }
    if (length == 0u) {
        return RoboOk;
    }

    while (cursor < length) {
        const char *start = text + cursor;
        const char *end;
        const char *entryStart;
        const char *entryEnd;
        LayoutFontFamily family;

        /* One entry runs to the next top-level comma, or the end. Quotes are honoured so that a comma
         * inside a quoted name does not end the entry -- the computed value usually arrives
         * de-quoted, but a caller may hand this door raw declaration text. */
        {
            int quoted = 0;
            size_t scan = cursor;

            while (scan < length) {
                char c = text[scan];

                if (quoted) {
                    if (c == '\\' && scan + 1u < length) {
                        scan += 2u;
                        continue;
                    }
                    if (c == quoted) {
                        quoted = 0;
                    }
                } else if (c == '"' || c == '\'') {
                    quoted = c;
                } else if (c == ',') {
                    break;
                }
                scan += 1u;
            }
            end = text + scan;
            cursor = (scan < length) ? scan + 1u : scan;
        }

        entryStart = start;
        entryEnd = end;
        LayoutTrim(&entryStart, &entryEnd);

        /* Strip one surrounding quote pair if the entry still carries one. */
        if (entryEnd - entryStart >= 2 &&
            (entryStart[0] == '"' || entryStart[0] == '\'') &&
            entryStart[0] == entryEnd[-1]) {
            entryStart += 1;
            entryEnd -= 1;
            LayoutTrim(&entryStart, &entryEnd);
        }

        if (entryEnd <= entryStart) {
            /* An empty entry (a stray comma) is not a family and is skipped rather than failing. */
            continue;
        }

        LayoutFontFamilySet(&family, entryStart, (size_t)(entryEnd - entryStart));
        (void)LayoutFontFamilyListAppend(out, &family);
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The request                                                         */
/* ------------------------------------------------------------------ */

/** The one entry the initial family list carries: `serif`, the registry's own spelled initial. */
static void LayoutFontRequestInitialFamilies(LayoutFontFamilyList *list)
{
    LayoutFontFamily family;

    LayoutFontFamilyListInit(list);
    LayoutFontFamilyInit(&family);
    family.kind = LayoutFontFamilyGeneric;
    family.generic = RoboFontGenericSerif;
    (void)LayoutFontFamilyListAppend(list, &family);
}

void LayoutFontRequestInit(LayoutFontRequest *request)
{
    if (!request) {
        return;
    }
    LayoutFontRequestInitialFamilies(&request->families);
    request->sizePx = RoboLayoutTextInitialFontSizePx;
    request->weight = RoboLayoutTextWeightNormal;
    request->stretch = RoboFontStretchNormal;
    request->italic = 0;
    request->direction = RoboLayoutTextDirectionLtr;
    request->familyUnresolved = 0;
}

/** The `font-family` list @p computed declares, or the initial list. */
static void LayoutFontRequestFamilies(const CssComputedStyle *computed,
                                      LayoutFontRequest *request)
{
    const CssComputedValue *value;
    CssSlice text;

    if (!computed) {
        request->familyUnresolved = 1;
        return;
    }
    value = CssComputedStyleComputedByName(computed,
                                           CssSliceFromText("font-family"));
    if (!value) {
        /* Absent: the initial list, and *not* an unresolved declaration. */
        return;
    }
    if (value->kind != RoboCssComputedValueFamilyList) {
        request->familyUnresolved = 1;
        return;
    }
    text = CssComputedValueText(value);
    if (CssSliceIsEmpty(text)) {
        request->familyUnresolved = 1;
        return;
    }
    (void)LayoutFontFamilyListParse((const char *)text.bytes, text.length,
                                    &request->families);
    if (LayoutFontFamilyListIsEmpty(&request->families)) {
        LayoutFontRequestInitialFamilies(&request->families);
        request->familyUnresolved = 1;
    }
}

/** The nine CSS stretch keywords, mapped to RoboFont's percentages. */
static int LayoutFontStretchKeyword(CssSlice keyword, FontStretch *out)
{
    static const struct {
        const char *name;
        FontStretch stretch;
    } table[] = {
        { "ultra-condensed", RoboFontStretchUltraCondensed },
        { "extra-condensed", RoboFontStretchExtraCondensed },
        { "condensed", RoboFontStretchCondensed },
        { "semi-condensed", RoboFontStretchSemiCondensed },
        { "normal", RoboFontStretchNormal },
        { "semi-expanded", RoboFontStretchSemiExpanded },
        { "expanded", RoboFontStretchExpanded },
        { "extra-expanded", RoboFontStretchExtraExpanded },
        { "ultra-expanded", RoboFontStretchUltraExpanded }
    };
    size_t index;

    for (index = 0u; index < sizeof(table) / sizeof(table[0]); index++) {
        if (CssSliceEqualsAsciiCi(keyword, table[index].name)) {
            *out = table[index].stretch;
            return 1;
        }
    }
    return 0;
}

/**
 * The `font-stretch` @p computed declares, or `normal`.
 *
 * Read best-effort: the CSS layer does not yet register `font-stretch` as a computed property (the
 * phase's §13 explicitly allows deferring it), so this reads what a *recognizable* value carries -- a
 * PERCENTAGE or one of the nine keywords -- and otherwise answers the initial `normal`. That keeps the
 * integration path defined without blocking the phase on a CSS registration.
 */
static FontStretch LayoutFontRequestStretch(const CssComputedStyle *computed)
{
    const CssComputedValue *value;

    if (!computed) {
        return RoboFontStretchNormal;
    }
    value = CssComputedStyleComputedByName(computed,
                                           CssSliceFromText("font-stretch"));
    if (!value) {
        return RoboFontStretchNormal;
    }
    if (value->kind == RoboCssComputedValuePercentage ||
        value->kind == RoboCssComputedValueNumber) {
        double percent = value->number;

        if (percent >= 50.0 && percent <= 200.0) {
            return (FontStretch)percent;
        }
        return RoboFontStretchNormal;
    }
    if (value->kind == RoboCssComputedValueKeyword) {
        FontStretch stretch = RoboFontStretchNormal;

        if (LayoutFontStretchKeyword(CssComputedValueText(value), &stretch)) {
            return stretch;
        }
    }
    return RoboFontStretchNormal;
}

Result LayoutFontRequestResolve(const CssComputedStyle *computed,
                                LayoutFontRequest *outRequest)
{
    LayoutTextStyle scalars;

    if (!outRequest) {
        return RoboInvalidArgument;
    }
    LayoutFontRequestInit(outRequest);

    /*
     * The scalar half is the measurer's own resolver, called rather than re-implemented: a size, a
     * weight, the slope flag and the direction therefore mean the same thing in this record and in the
     * `LayoutTextStyle` a measurer is handed, which is the one-place rule the phase asks for.
     */
    if (LayoutTextStyleResolve(computed, &scalars) == RoboOk) {
        outRequest->sizePx = scalars.fontSizePx;
        outRequest->weight = scalars.fontWeight;
        outRequest->italic = scalars.italic;
        outRequest->direction = scalars.direction;
    }

    outRequest->stretch = LayoutFontRequestStretch(computed);
    LayoutFontRequestFamilies(computed, outRequest);
    return RoboOk;
}

int LayoutFontRequestEqual(const LayoutFontRequest *a, const LayoutFontRequest *b)
{
    if (!a || !b) {
        return !a && !b;
    }
    return a->sizePx == b->sizePx &&
           a->weight == b->weight &&
           a->stretch == b->stretch &&
           a->italic == b->italic &&
           a->direction == b->direction &&
           a->familyUnresolved == b->familyUnresolved &&
           LayoutFontFamilyListEqual(&a->families, &b->families);
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

/** Append @p text to a bounded writer; returns 0 when it does not fit. */
static int LayoutFontAppend(char *buffer,
                            size_t capacity,
                            size_t *cursor,
                            const char *text)
{
    size_t length = strlen(text);

    if (*cursor + length + 1u > capacity) {
        return 0;
    }
    memcpy(buffer + *cursor, text, length);
    *cursor += length;
    buffer[*cursor] = '\0';
    return 1;
}

Result LayoutFontRequestWrite(const LayoutFontRequest *request,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength)
{
    size_t cursor = 0u;
    uint32_t index;

    if (!request || !buffer || capacity == 0u) {
        return RoboInvalidArgument;
    }
    buffer[0] = '\0';

    if (!LayoutFontAppend(buffer, capacity, &cursor, "families=")) {
        return RoboNotSupported;
    }
    if (LayoutFontFamilyListIsEmpty(&request->families) &&
        !LayoutFontAppend(buffer, capacity, &cursor, "none")) {
        return RoboNotSupported;
    }
    for (index = 0u; index < request->families.count; index++) {
        char entry[RoboLayoutFontMaxFamilyBytes + 2u];

        if (index > 0u && !LayoutFontAppend(buffer, capacity, &cursor, ", ")) {
            return RoboNotSupported;
        }
        entry[0] = '\0';
        if (request->families.entries[index].kind == LayoutFontFamilyNamed) {
            strncat(entry, request->families.entries[index].name,
                    sizeof(entry) - 2u);
        } else {
            strncat(entry, FontGenericFamilyName(
                               request->families.entries[index].generic),
                    sizeof(entry) - 2u);
        }
        if (!LayoutFontAppend(buffer, capacity, &cursor, entry)) {
            return RoboNotSupported;
        }
    }

    {
        char scalars[96];

        (void)snprintf(scalars, sizeof(scalars),
                       " size=%.2f weight=%d stretch=%u %s %s",
                       request->sizePx, (int)request->weight,
                       (unsigned)request->stretch,
                       request->italic ? "italic" : "upright",
                       request->direction == RoboLayoutTextDirectionRtl ? "rtl"
                                                                       : "ltr");
        if (!LayoutFontAppend(buffer, capacity, &cursor, scalars)) {
            return RoboNotSupported;
        }
    }
    if (outLength) {
        *outLength = cursor;
    }
    return RoboOk;
}
