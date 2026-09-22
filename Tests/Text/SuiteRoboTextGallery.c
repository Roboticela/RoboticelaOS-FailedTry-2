/*
 * The RoboText glyph table (Phase 12.4) — `text.gallery.*`.
 *
 * The phase asks for a development surface showing, side by side, the raw text, its text runs, the
 * glyph ids, the advances and a rendered glyph run. This is the half of that the text layer can own:
 * the **numbers**, written to `Build/Text-GlyphTable.txt` for every string the phase names, so a reader
 * (or a diff) can see what the pipeline decided rather than infer it from a width.
 *
 * The other half is pixels, and it deliberately does not live here. `RoboText` links no rasterizer — the
 * boundary scan forbids `RoboGraphics` and `RoboRender` — so a suite in this directory could not draw a
 * glyph if it wanted to. A rendered run is the *desktop's* surface, and since the paint pass generates
 * its glyphs through `RoboTextShape` the three existing galleries are that drawing:
 *
 *   Build/Typography-Desktop.png     the shell's chrome, drawn through the resolver *and* RoboText
 *   Build/Typography-Chrome.png      window title bands, likewise
 *   Build/Typography-Document.png    a specimen page whose lines are RoboText-measured and RoboText-painted
 *
 * (`Tests/Desktop/SuiteRoboDesktopTypographyGallery.c`; the QEMU run's `ROBO_QEMU_DESKTOP_PIXELS`
 * checksum is that same picture on the guest, and it is unchanged from before the 12.4 migration.)
 *
 * ## What the table asserts, given that a dump is not a verdict
 *
 * A written file proves nothing by itself, so every row below is *checked* as it is written:
 *
 *   **the table is the measurement.** The glyph rows' advances sum to `measurement.width`, so a reader
 *   comparing the two columns is comparing numbers the module itself keeps identity on. A second
 *   arithmetic path inside the pipeline would show up as a mismatch here first.
 *
 *   **the glyphs are the glyph runs.** Row count equals `measurement.glyphCount`, and every glyph's own
 *   `source` range is non-empty and inside the text — a glyph that named scalars the run does not cover
 *   would be a mapping bug the run's own counters could not see.
 *
 *   **it is a reference image in text form.** The report is built twice and compared byte for byte. That
 *   is the same determinism claim the shape suite makes over values, made over the *rendering of* those
 *   values — which is what a person diffing two runs of this test actually has.
 */

#include "RoboTextTestSupport.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* The evidence                                                        */
/* ------------------------------------------------------------------ */

#define GALLERY_PATH "Build/Text-GlyphTable.txt"

/** The longest dump line the module produces, with room for the row's own prefix. */
#define GALLERY_LINE_MAX 320u

/** Big enough for every sample at every glyph, and a compile-time bound rather than a growable buffer. */
#define GALLERY_REPORT_MAX (1u << 20)

/* ------------------------------------------------------------------ */
/* The report                                                          */
/* ------------------------------------------------------------------ */

/**
 * A report under construction: a growable-to-nothing byte buffer, a length and a truncation flag.
 *
 * The storage is **allocated**, not a member array, and the reason is the host's stack: a one-megabyte
 * struct declared as a local overflows the 1 MB thread stack the moment the suite starts, which is a
 * crash before the first case rather than a failure in one.
 */
typedef struct galleryReport {
    char *bytes;
    size_t length;
    size_t capacity;
    /** Set when a line did not fit, so the case fails loudly rather than writing a truncated table. */
    int truncated;
} GalleryReport;

static void GalleryInit(GalleryReport *report)
{
    memset(report, 0, sizeof(*report));
    report->capacity = GALLERY_REPORT_MAX;
    report->bytes = (char *)Malloc(report->capacity);
    RoboCheckMsg(report->bytes != NULL, "the gallery report could not be allocated");
    report->bytes[0] = '\0';
}

static void GalleryClose(GalleryReport *report)
{
    if (!report) {
        return;
    }
    RoboFree(report->bytes);
    report->bytes = NULL;
    report->capacity = 0u;
    report->length = 0u;
}

/** Append one formatted line. A line that does not fit marks the report rather than truncating it. */
static void GalleryAppend(GalleryReport *report, const char *format, ...)
{
    va_list args;
    size_t remaining;
    int written;

    if (report->truncated || report->length + 1u >= report->capacity) {
        report->truncated = 1;
        return;
    }
    remaining = report->capacity - report->length;
    va_start(args, format);
    written = vsnprintf(report->bytes + report->length, remaining, format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= remaining) {
        report->truncated = 1;
        return;
    }
    report->length += (size_t)written;
}

/* ------------------------------------------------------------------ */
/* The strings the phase names                                         */
/* ------------------------------------------------------------------ */

typedef struct gallerySample {
    /** The name a reader scans for. */
    const char *name;
    /** The text, as UTF-8. */
    const char *text;
    /** Glyphs the sample produces; filled by `GallerySampleRow`. */
    size_t glyphs;
    /** Scalars the face had no glyph for; filled by `GallerySampleRow`. */
    size_t missing;
} GallerySample;

/**
 * Every string the phase names, plus the noncharacter.
 *
 * The noncharacter (`U+FDD0`) is here and the other Unicode samples are here for a different reason: the
 * Unicode samples show that multibyte text decodes and measures, and the noncharacter is the one sample
 * whose absence from *any* conforming face is guaranteed, so its row is where `missing=1` and `.notdef`'s
 * own advance can be read.
 */
static GallerySample kGallerySamples[] = {
    { "hello", TextSampleHello, 0u, 0u },
    { "typography", TextSampleTypography, 0u, 0u },
    { "pangram", TextSamplePangram, 0u, 0u },
    { "upper", TextSampleUpper, 0u, 0u },
    { "lower", TextSampleLower, 0u, 0u },
    { "digits", TextSampleDigits, 0u, 0u },
    { "unicode", TextSampleUnicode, 0u, 0u },
    { "noncharacter", TextSampleNoncharacter, 0u, 0u },
};

#define GALLERY_SAMPLE_COUNT (sizeof(kGallerySamples) / sizeof(kGallerySamples[0]))

/**
 * Write one sample's block into @p report and fill @p sample's counts.
 *
 * Every number in the block comes from the module: the run rows from the run set, the glyph rows from
 * `RoboTextGlyphWrite` (which is the module's own dump — the same function a developer would call), and
 * the summary line from the measurement. Nothing here formats a value it computed itself, which is what
 * makes the file evidence rather than commentary.
 */
static void GallerySampleRow(GalleryReport *report, const TextFixture *fixture, GallerySample *sample)
{
    RoboTextView view = RoboTextViewMake(sample->text, strlen(sample->text));
    RoboTextShapeResult *result = NULL;
    const RoboTextMeasurement *measurement;
    const RoboTextRunSet *runs;
    double advanceSum = 0.0;
    size_t index;
    size_t glyphs = 0u;

    RoboCheckMsg(RoboTextShape(view, &fixture->font, NULL, &result, NULL) == RoboOk,
                 "a required string would not shape");
    RoboCheckMsg(result != NULL, "shape answered OK with no result");

    runs = RoboTextShapeResultRuns(result);
    measurement = RoboTextShapeResultMeasurement(result);
    RoboCheckMsg(runs != NULL, "a shape result has no run set");
    RoboCheckMsg(measurement != NULL, "a shape result has no measurement");

    GalleryAppend(report, "text        %s\n", sample->name);
    GalleryAppend(report, "  raw       \"%s\"\n", sample->text);
    GalleryAppend(report, "  input     bytes=%u scalars=%u text-runs=%u script-runs=%u\n",
                  (unsigned)strlen(sample->text),
                  (unsigned)RoboTextRunSetScalarCount(runs),
                  (unsigned)RoboTextRunSetTextRunCount(runs),
                  (unsigned)RoboTextRunSetScriptRunCount(runs));

    /* The runs: script, classified direction, source span and language, one row each. */
    for (index = 0u; index < RoboTextRunSetTextRunCount(runs); index++) {
        const RoboTextRun *run = RoboTextRunSetTextRun(runs, index);
        RoboCheckMsg(run != NULL, "a text run index in range answered no run");
        GalleryAppend(report, "  run[%u]    %s %s [%u,%u) lang=%s\n",
                      (unsigned)index,
                      RoboTextScriptName(run->script),
                      RoboTextDirectionName(run->direction),
                      (unsigned)run->range.start,
                      (unsigned)run->range.end,
                      RoboTextLanguageIsSet(&run->language) ? RoboTextLanguageTag(&run->language)
                                                            : "-");
    }

    /* The glyphs, in the order a painter walks them, through the module's own dump line. */
    for (index = 0u; index < RoboTextShapeResultGlyphRunCount(result); index++) {
        const RoboTextGlyphRun *glyphRun = RoboTextShapeResultGlyphRun(result, index);
        GalleryAppend(report, "  glyph-run[%u] %s %s glyphs=%u advance=%.2f missing=%u\n",
                      (unsigned)index,
                      RoboTextScriptName(RoboTextGlyphRunScript(glyphRun)),
                      RoboTextDirectionName(RoboTextGlyphRunDirection(glyphRun)),
                      (unsigned)RoboTextGlyphRunCount(glyphRun),
                      (double)RoboTextGlyphRunAdvance(glyphRun),
                      (unsigned)RoboTextGlyphRunMissingGlyphs(glyphRun));

        {
            size_t glyphIndex;

            for (glyphIndex = 0u; glyphIndex < RoboTextGlyphRunCount(glyphRun); glyphIndex++) {
                const RoboTextGlyph *glyph =
                    RoboTextGlyphRunGlyph(glyphRun, (RoboTextGlyphIndex)glyphIndex);
                char line[GALLERY_LINE_MAX];
                size_t needed = 0u;

                RoboCheckMsg(glyph != NULL, "a glyph index in range answered no glyph");
                memset(line, 0, sizeof(line));
                RoboCheckMsg(RoboTextGlyphWrite(glyph, line, sizeof(line), &needed) == RoboOk,
                             "a glyph dump line did not fit the gallery's own line buffer");
                GalleryAppend(report, "  g[%u]      %s\n", (unsigned)glyphs, line);

                /* The mapping claim: a glyph names scalars, and they are inside the text it came from. */
                RoboCheckMsg(RoboTextRangeLength(glyph->source) > 0u,
                             "a glyph was generated from no scalar at all");
                RoboCheckMsg(RoboTextRangeIsWithin(glyph->source,
                                                   RoboTextRunSetScalarCount(runs)),
                             "a glyph's source range falls outside the text it was generated from");
                advanceSum += (double)glyph->advanceX;
                glyphs++;
            }
        }
    }

    GalleryAppend(report,
                  "  measures  width=%.2f ascent=%.2f descent=%.2f line-height=%.2f glyphs=%u "
                  "missing=%u advance-total=%.2f\n",
                  (double)measurement->width,
                  (double)measurement->ascent,
                  (double)measurement->descent,
                  (double)measurement->lineHeight,
                  (unsigned)measurement->glyphCount,
                  (unsigned)measurement->missingGlyphs,
                  advanceSum);
    GalleryAppend(report, "\n");

    /* The table *is* the measurement: the rows' advances sum to the width, exactly as the module does. */
    RoboCheckEqInt((int)glyphs, (int)measurement->glyphCount);
    {
        float difference = (float)(advanceSum - (double)measurement->width);

        /* One rounding at the run boundary is allowed; a second arithmetic path is not. */
        RoboCheckMsg(difference < 0.02f && difference > -0.02f,
                     "the glyph rows' advances do not sum to the measurement's width");
    }

    sample->glyphs = glyphs;
    sample->missing = measurement->missingGlyphs;
    RoboTextShapeResultDestroy(result);
}

/**
 * Build the whole table over the **shipped** face.
 *
 * The face and its setting are written into the header, because a table of glyph ids and advances is
 * evidence about *a* font at *a* size, and a reader two years from now should not have to guess which.
 */
static void GalleryBuild(GalleryReport *report)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextFontStyle style = RoboTextFontContextStyle(&fixture.font);
    char setting[GALLERY_LINE_MAX];
    size_t needed = 0u;
    size_t index;

    memset(setting, 0, sizeof(setting));
    RoboCheckMsg(RoboTextFontStyleWrite(&style, setting, sizeof(setting), &needed) == RoboOk,
                 "the font setting would not write into the gallery's own line buffer");

    GalleryInit(report);
    GalleryAppend(report, "RoboText glyph table (Phase 12.4)\n");
    GalleryAppend(report, "  source    System/Fonts/ (the shipped system typeface pack)\n");
    GalleryAppend(report, "  setting   %s\n", setting);
    GalleryAppend(report, "  generation cmap-per-scalar (the placeholder; no substitution, no positioning)\n");
    GalleryAppend(report, "\n");

    for (index = 0u; index < GALLERY_SAMPLE_COUNT; index++) {
        GallerySampleRow(report, &fixture, &kGallerySamples[index]);
    }

    GalleryAppend(report, "Legend  run[i] script direction [first,last)   the text run's own classification\n");
    GalleryAppend(report, "        g[i]   id scalar[first,last) advance units offset mapped\n");
    GalleryAppend(report, "               advance is in device pixels, units is the face's own hmtx value\n");
    GalleryAppend(report, "        mapped=0 is .notdef: the face has no glyph, and the pen still moved\n");

    TextFixtureClose(&fixture);
}

/** Write @p report to `Build/`, and say so — a developer should not have to hunt for the file. */
static void GalleryWrite(const GalleryReport *report)
{
    FILE *file = fopen(GALLERY_PATH, "wb");

    RoboCheckMsg(file != NULL, "the gallery report could not be opened for writing");
    RoboCheckEqInt((int)fwrite(report->bytes, 1u, report->length, file), (int)report->length);
    fclose(file);
    printf("       wrote %s (%u bytes)\n", GALLERY_PATH, (unsigned)report->length);
}

/* ------------------------------------------------------------------ */
/* The cases                                                           */
/* ------------------------------------------------------------------ */

/**
 * The table itself, and the two claims a reader can check inside it.
 *
 * The per-row assertions live in `GallerySampleRow` (they are claims about one string), and the two here
 * are claims about the *set*: every named string is in the file, and the one string no face can draw is
 * counted rather than measured as if it had been drawn.
 */
static void TestGlyphTable(void)
{
    GalleryReport report;
    size_t index;

    GalleryBuild(&report);
    RoboCheckMsg(!report.truncated, "the gallery report overflowed its own bound");

    for (index = 0u; index < GALLERY_SAMPLE_COUNT; index++) {
        const GallerySample *sample = &kGallerySamples[index];

        /* The raw text is in the file verbatim, so a reader can tell what was shaped. */
        RoboCheckMsg(strstr(report.bytes, sample->text) != NULL,
                     "a required string's raw text is missing from the table");
        RoboCheckMsg(sample->glyphs > 0u, "a required string produced no glyph at all");
        printf("       %-14s glyphs=%u missing=%u\n",
               sample->name,
               (unsigned)sample->glyphs,
               (unsigned)sample->missing);
    }

    /* U+FDD0 exists in Unicode and in no conforming font: the row is where a counted miss is visible. */
    RoboCheckMsg(kGallerySamples[GALLERY_SAMPLE_COUNT - 1u].missing == 1u,
                 "a noncharacter was measured as if the face had a glyph for it");

    GalleryWrite(&report);
    GalleryClose(&report);
}

/**
 * The table is a *reference*, and a reference is only useful if two runs produce one file.
 *
 * The shape suite already asserts determinism over the values; this asserts it over the artifact a person
 * compares, which is the thing a screenshot test stands on.
 */
static void TestGlyphTableDeterminism(void)
{
    GalleryReport first;
    GalleryReport second;

    GalleryBuild(&first);
    GalleryBuild(&second);

    RoboCheckEqInt((int)first.length, (int)second.length);
    RoboCheckMsg(memcmp(first.bytes, second.bytes, first.length) == 0,
                 "two builds of the glyph table differ");
    RoboCheckMsg(!first.truncated && !second.truncated, "a gallery report overflowed its own bound");

    GalleryClose(&first);
    GalleryClose(&second);
}

void TestRegisterTextGallery(void)
{
    TestRegister("text.gallery.glyph_table", TestGlyphTable);
    TestRegister("text.gallery.determinism", TestGlyphTableDeterminism);
}
