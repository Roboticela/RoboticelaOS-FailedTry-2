/*
 * RoboFontSystemPack — the RoboticelaOS system typeface pack (Phase 12.2).
 * See RoboFontSystemPack.h for the contract this file implements.
 *
 * The file is two halves and the second one is short on purpose:
 *
 *   the parser   pure text → FontSystemManifest. It does no IO and knows no
 *                paths, so invalid manifests are testable without a filesystem.
 *
 *   the pack     manifest + FontDiscoveryIo → registry entries → (optionally)
 *                FontLoader cache → FontSelection. Every step is an existing
 *                Phase 5 API; this module adds *binding*, not parsing.
 *
 * Two rules are deliberately strict, because they are what make "the image
 * shipped its system font" a checkable claim rather than a hope: a manifest
 * that cannot be read is an error, and a family whose registered faces do not
 * resolve to the declared family is an error. A pack never degrades to a
 * partial install.
 */

#include "RoboFont/RoboFontSystemPack.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Small ASCII helpers (no locale, no libc string classification)      */
/* ------------------------------------------------------------------ */

static size_t PackLen(const char *s)
{
    size_t n = 0u;

    if (!s) {
        return 0u;
    }
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

static int IsSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
           c == '\v';
}

static char LowerAscii(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

/** ASCII case-insensitive equality of a span and a NUL-terminated word. */
static int SpanWordEq(const char *span, size_t spanLen, const char *word)
{
    size_t i;
    size_t wlen = PackLen(word);

    if (spanLen != wlen) {
        return 0;
    }
    for (i = 0u; i < spanLen; i++) {
        if (LowerAscii(span[i]) != LowerAscii(word[i])) {
            return 0;
        }
    }
    return 1;
}

static int WordEqN(const char *a, size_t alen, const char *b, size_t blen)
{
    size_t i;

    if (alen != blen) {
        return 0;
    }
    for (i = 0u; i < alen; i++) {
        if (LowerAscii(a[i]) != LowerAscii(b[i])) {
            return 0;
        }
    }
    return 1;
}

static void CopySpan(char *dst, size_t cap, const char *src, size_t len)
{
    size_t n = len;

    if (!dst || cap == 0u) {
        return;
    }
    if (n >= cap) {
        n = cap - 1u;
    }
    if (n > 0u && src) {
        memcpy(dst, src, n);
    }
    dst[n] = '\0';
}

static Result ParseUint(const char *s, size_t len, uint32_t *out)
{
    uint32_t value = 0u;
    size_t i;
    int any = 0;

    if (!s || !out || len == 0u) {
        return RoboParseError;
    }
    for (i = 0u; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return RoboParseError;
        }
        if (value > 429496729u) {
            return RoboParseError;
        }
        value = value * 10u + (uint32_t)(s[i] - '0');
        any = 1;
    }
    if (!any) {
        return RoboParseError;
    }
    *out = value;
    return RoboOk;
}

/** Minimal decimal float: [-+]digits[.digits]. No exponent (axis values). */
static Result ParseFloat(const char *s, size_t len, float *out)
{
    size_t i = 0u;
    int negative = 0;
    double value = 0.0;
    double scale = 1.0;
    int seenDigit = 0;
    int seenDot = 0;

    if (!s || !out || len == 0u) {
        return RoboParseError;
    }
    if (s[i] == '+' || s[i] == '-') {
        negative = (s[i] == '-');
        i++;
    }
    for (; i < len; i++) {
        char c = s[i];

        if (c == '.') {
            if (seenDot) {
                return RoboParseError;
            }
            seenDot = 1;
            continue;
        }
        if (c < '0' || c > '9') {
            return RoboParseError;
        }
        seenDigit = 1;
        if (seenDot) {
            scale *= 0.1;
            value += (double)(c - '0') * scale;
        } else {
            value = value * 10.0 + (double)(c - '0');
        }
    }
    if (!seenDigit) {
        return RoboParseError;
    }
    *out = (float)(negative ? -value : value);
    return RoboOk;
}

static Result ParseTag(const char *s, size_t len, FontTag *out)
{
    if (!s || !out || len != 4u) {
        return RoboParseError;
    }
    *out = RoboFontTag(s[0], s[1], s[2], s[3]);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Roles                                                               */
/* ------------------------------------------------------------------ */

const char *RoboFontSystemRoleName(RoboFontSystemRole role)
{
    switch (role) {
    case RoboFontSystemRoleUIDisplay:
        return "display";
    case RoboFontSystemRoleUIText:
        return "text";
    case RoboFontSystemRoleMonospace:
        return "monospace";
    default:
        return NULL;
    }
}

int RoboFontSystemRoleIsValid(RoboFontSystemRole role)
{
    return RoboFontSystemRoleName(role) != NULL;
}

Result RoboFontSystemRoleFromName(const char *name,
                                                  size_t nameLen,
                                                  RoboFontSystemRole *out)
{
    RoboFontSystemRole role;

    if (!name || !out) {
        return RoboInvalidArgument;
    }
    if (nameLen == 0u) {
        nameLen = PackLen(name);
    }
    for (role = RoboFontSystemRoleUIDisplay; role <= RoboFontSystemRoleMonospace;
         role = (RoboFontSystemRole)(role + 1)) {
        const char *candidate = RoboFontSystemRoleName(role);
        if (SpanWordEq(name, nameLen, candidate)) {
            *out = role;
            return RoboOk;
        }
    }
    return RoboNotFound;
}

/* ------------------------------------------------------------------ */
/* Manifest parser                                                     */
/* ------------------------------------------------------------------ */

void FontSystemManifestInit(FontSystemManifest *manifest)
{
    if (!manifest) {
        return;
    }
    memset(manifest, 0, sizeof(*manifest));
}

static FontSystemFamily *ManifestCurrentFamily(FontSystemManifest *m,
                                                int *open)
{
    if (!m || m->familyCount == 0u) {
        return NULL;
    }
    *open = 1;
    return &m->families[m->familyCount - 1u];
}

static int ManifestHasFamilyName(const FontSystemManifest *m,
                                 const char *name,
                                 size_t nameLen)
{
    uint32_t i;

    for (i = 0u; i < m->familyCount; i++) {
        if (WordEqN(m->families[i].family, PackLen(m->families[i].family), name,
                    nameLen)) {
            return 1;
        }
    }
    return 0;
}

static Result ManifestAddRole(FontSystemFamily *fam,
                              const char *name,
                              size_t nameLen,
                              int hasWeight,
                              FontWeight weight)
{
    RoboFontSystemRole role;

    if (fam->roleCount >= RoboFontSystemMaxRolesPerFamily) {
        return RoboParseError;
    }
    if (RoboFontSystemRoleFromName(name, nameLen, &role) != RoboOk) {
        return RoboParseError;
    }
    fam->roles[fam->roleCount].role = role;
    fam->roles[fam->roleCount].weight = hasWeight ? weight : (FontWeight)0u;
    fam->roleCount++;
    return RoboOk;
}

Result FontSystemManifestParse(const char *text,
                                              size_t length,
                                              FontSystemManifest *out)
{
    FontSystemManifest scratch;
    const char *p;
    const char *end;
    int open = 0;

    if (!out) {
        return RoboInvalidArgument;
    }
    if (!text || length == 0u) {
        return RoboInvalidArgument;
    }
    FontSystemManifestInit(&scratch);
    p = text;
    end = text + length;

    while (p < end) {
        const char *lineEnd = p;
        const char *word;
        const char *wordEnd;
        const char *rest;
        const char *restEnd;
        size_t wordLen;
        size_t restLen;

        while (lineEnd < end && *lineEnd != '\n') {
            lineEnd++;
        }
        word = p;
        while (word < lineEnd && IsSpace(*word)) {
            word++;
        }
        if (word >= lineEnd || *word == '#') {
            p = (lineEnd < end) ? lineEnd + 1 : end;
            continue;
        }
        wordEnd = word;
        while (wordEnd < lineEnd && !IsSpace(*wordEnd)) {
            wordEnd++;
        }
        rest = wordEnd;
        while (rest < lineEnd && IsSpace(*rest)) {
            rest++;
        }
        restEnd = lineEnd;
        while (restEnd > rest && IsSpace(restEnd[-1])) {
            restEnd--;
        }
        wordLen = (size_t)(wordEnd - word);
        restLen = (size_t)(restEnd - rest);

        if (SpanWordEq(word, wordLen, "family")) {
            if (open) {
                return RoboParseError; /* unterminated previous family */
            }
            if (restLen == 0u) {
                return RoboParseError;
            }
            if (scratch.familyCount >= RoboFontSystemMaxFamilies) {
                return RoboParseError;
            }
            if (ManifestHasFamilyName(&scratch, rest, restLen)) {
                return RoboParseError; /* duplicate family */
            }
            memset(&scratch.families[scratch.familyCount], 0,
                   sizeof(scratch.families[scratch.familyCount]));
            CopySpan(scratch.families[scratch.familyCount].family,
                     RoboFontSystemMaxNameBytes, rest, restLen);
            scratch.familyCount++;
            open = 1;
        } else if (SpanWordEq(word, wordLen, "end")) {
            if (!open) {
                return RoboParseError;
            }
            if (restLen != 0u) {
                return RoboParseError;
            }
            open = 0;
        } else if (open) {
            FontSystemFamily *fam = ManifestCurrentFamily(&scratch, &open);
            Result rc = RoboOk;

            if (!fam) {
                return RoboParseError;
            }
            if (SpanWordEq(word, wordLen, "file")) {
                if (restLen == 0u) {
                    return RoboParseError;
                }
                CopySpan(fam->file, RoboFontSystemMaxPathBytes, rest, restLen);
            } else if (SpanWordEq(word, wordLen, "role")) {
                const char *roleName = rest;
                const char *roleEnd = restEnd;
                const char *roleWord = rest;
                int hasWeight = 0;
                FontWeight weight = 0u;

                while (roleWord < roleEnd && !IsSpace(*roleWord)) {
                    roleWord++;
                }
                if (roleWord >= roleEnd) {
                    return RoboParseError;
                }
                {
                    const char *tail = roleWord;
                    const char *tailEnd = roleEnd;

                    while (tail < tailEnd && IsSpace(*tail)) {
                        tail++;
                    }
                    if (tail >= tailEnd) {
                        return RoboParseError;
                    }
                    {
                        const char *kw = tail;
                        while (kw < tailEnd && !IsSpace(*kw)) {
                            kw++;
                        }
                        if (!WordEqN(tail, (size_t)(kw - tail), "weight",
                                     PackLen("weight"))) {
                            return RoboParseError;
                        }
                        while (kw < tailEnd && IsSpace(*kw)) {
                            kw++;
                        }
                        {
                            uint32_t w = 0u;
                            if (ParseUint(kw, (size_t)(tailEnd - kw), &w) !=
                                RoboOk) {
                                return RoboParseError;
                            }
                            if (!FontWeightIsValid((FontWeight)w)) {
                                return RoboParseError;
                            }
                            hasWeight = 1;
                            weight = (FontWeight)w;
                        }
                    }
                }
                rc = ManifestAddRole(fam, roleName,
                                     (size_t)(roleWord - roleName), hasWeight,
                                     weight);
                if (rc != RoboOk) {
                    return rc;
                }
            } else if (SpanWordEq(word, wordLen, "weight")) {
                uint32_t w = 0u;
                if (ParseUint(rest, restLen, &w) != RoboOk ||
                    !FontWeightIsValid((FontWeight)w)) {
                    return RoboParseError;
                }
                fam->weight = (FontWeight)w;
            } else if (SpanWordEq(word, wordLen, "stretch")) {
                uint32_t s = 0u;
                if (ParseUint(rest, restLen, &s) != RoboOk ||
                    !FontStretchIsValid((FontStretch)s)) {
                    return RoboParseError;
                }
                fam->stretch = (FontStretch)s;
            } else if (SpanWordEq(word, wordLen, "style")) {
                if (SpanWordEq(rest, restLen, "normal")) {
                    fam->style = RoboFontStyleNormal;
                } else if (SpanWordEq(rest, restLen, "italic")) {
                    fam->style = RoboFontStyleItalic;
                } else if (SpanWordEq(rest, restLen, "oblique")) {
                    fam->style = RoboFontStyleOblique;
                } else {
                    return RoboParseError;
                }
            } else if (SpanWordEq(word, wordLen, "variable")) {
                fam->variable = 1u;
            } else if (SpanWordEq(word, wordLen, "axis")) {
                FontSystemAxis *axis;
                const char *fields[4];
                size_t fieldLens[4];
                const char *q = rest;
                uint32_t fi = 0u;

                if (fam->axisCount >= RoboFontSystemMaxAxesPerFamily) {
                    return RoboParseError;
                }
                while (fi < 4u && q < restEnd) {
                    const char *fEnd;
                    while (q < restEnd && IsSpace(*q)) {
                        q++;
                    }
                    if (q >= restEnd) {
                        break;
                    }
                    fEnd = q;
                    while (fEnd < restEnd && !IsSpace(*fEnd)) {
                        fEnd++;
                    }
                    fields[fi] = q;
                    fieldLens[fi] = (size_t)(fEnd - q);
                    fi++;
                    q = fEnd;
                }
                if (fi != 4u) {
                    return RoboParseError;
                }
                axis = &fam->axes[fam->axisCount];
                if (ParseTag(fields[0], fieldLens[0], &axis->tag) != RoboOk ||
                    ParseFloat(fields[1], fieldLens[1], &axis->minValue) !=
                        RoboOk ||
                    ParseFloat(fields[2], fieldLens[2], &axis->defaultValue) !=
                        RoboOk ||
                    ParseFloat(fields[3], fieldLens[3], &axis->maxValue) !=
                        RoboOk) {
                    return RoboParseError;
                }
                if (axis->minValue > axis->maxValue ||
                    axis->defaultValue < axis->minValue ||
                    axis->defaultValue > axis->maxValue) {
                    return RoboParseError;
                }
                fam->axisCount++;
            } else if (SpanWordEq(word, wordLen, "license")) {
                if (restLen == 0u) {
                    return RoboParseError;
                }
                CopySpan(fam->licenseId, RoboFontSystemMaxNameBytes, rest,
                         restLen);
            } else if (SpanWordEq(word, wordLen, "source")) {
                CopySpan(fam->source, RoboFontSystemMaxTextBytes, rest, restLen);
            } else if (SpanWordEq(word, wordLen, "version")) {
                if (restLen == 0u) {
                    return RoboParseError;
                }
                CopySpan(fam->version, sizeof(fam->version), rest, restLen);
            } else {
                return RoboParseError;
            }
        } else if (SpanWordEq(word, wordLen, "system-font-pack")) {
            uint32_t v = 0u;
            if (ParseUint(rest, restLen, &v) != RoboOk) {
                return RoboParseError;
            }
            scratch.version = v;
        } else if (SpanWordEq(word, wordLen, "root")) {
            if (restLen == 0u) {
                return RoboParseError;
            }
            CopySpan(scratch.root, RoboFontSystemMaxPathBytes, rest, restLen);
        } else {
            return RoboParseError;
        }

        p = (lineEnd < end) ? lineEnd + 1 : end;
    }

    if (open) {
        return RoboParseError; /* missing `end` */
    }
    if (scratch.familyCount == 0u) {
        return RoboParseError;
    }
    {
        uint32_t i;
        for (i = 0u; i < scratch.familyCount; i++) {
            if (scratch.families[i].file[0] == '\0') {
                return RoboParseError; /* every family needs a file */
            }
        }
    }
    *out = scratch;
    return RoboOk;
}

const FontSystemFamily *FontSystemManifestFamily(
    const FontSystemManifest *manifest,
    const char *family)
{
    uint32_t i;

    if (!manifest || !family) {
        return NULL;
    }
    for (i = 0u; i < manifest->familyCount; i++) {
        if (WordEqN(manifest->families[i].family,
                    PackLen(manifest->families[i].family), family,
                    PackLen(family))) {
            return &manifest->families[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Pack                                                                */
/* ------------------------------------------------------------------ */

struct FontSystemPack {
    FontRegistry *registry;
    FontLoader *loader;
    char root[RoboFontSystemMaxPathBytes];
    FontSystemManifest manifest;
    FontFace **faces[RoboFontSystemMaxFamilies];
    uint32_t faceCount[RoboFontSystemMaxFamilies];
    uint32_t totalFaces;
    uint32_t loadFailures;
};

void FontSystemPackConfigInit(FontSystemPackConfig *config)
{
    if (!config) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->root = RoboFontSystemDefaultRoot;
    config->manifestName = RoboFontSystemManifestName;
    config->loadPolicy = RoboFontLoadPolicyStandard;
}

static void JoinPath(char *dst,
                     size_t cap,
                     const char *base,
                     const char *leaf)
{
    size_t n;
    size_t i = 0u;
    size_t baseLen = PackLen(base);
    size_t leafLen = PackLen(leaf);

    if (!dst || cap == 0u) {
        return;
    }
    for (n = 0u; n < baseLen && i + 1u < cap; n++) {
        dst[i++] = base[n];
    }
    if (i > 0u && dst[i - 1u] != '/' && i + 1u < cap) {
        dst[i++] = '/';
    }
    for (n = 0u; n < leafLen && i + 1u < cap; n++) {
        dst[i++] = leaf[n];
    }
    dst[i] = '\0';
}

static void BindVariation(FontSystemFamily *fam, FontFace *face)
{
    const FontVariationModel *model = NULL;

    if (!fam || !face) {
        return;
    }
    if (FontFaceParseVariation(face) != RoboOk) {
        return;
    }
    model = FontFaceVariation(face);
    if (!model || !FontVariationModelIsVariable(model)) {
        return;
    }
    fam->resolvedVariable = 1u;
    {
        uint32_t index = 0u;
        const FontVariationAxis *axis = NULL;

        if (FontVariationModelFindAxis(model, RoboFontTagWght, &index,
                                       &axis) == RoboOk &&
            axis) {
            fam->hasWghtAxis = 1u;
            fam->wghtMin = axis->minValue;
            fam->wghtDefault = axis->defaultValue;
            fam->wghtMax = axis->maxValue;
        }
    }
}

Result FontSystemPackOpen(FontRegistry *registry,
                                         FontLoader *loader,
                                         const FontSystemPackConfig *config,
                                         FontSystemPack **out)
{
    FontSystemPack *pack;
    FontSystemPackConfig local;
    char manifestPath[RoboFontSystemMaxPathBytes];
    uint8_t *bytes = NULL;
    size_t size = 0u;
    Result rc;
    uint32_t i;

    if (!registry || !config || !config->io || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    local = *config;
    if (!local.root || local.root[0] == '\0') {
        local.root = RoboFontSystemDefaultRoot;
    }
    if (!local.manifestName || local.manifestName[0] == '\0') {
        local.manifestName = RoboFontSystemManifestName;
    }
    if (local.loadPolicy == 0) {
        local.loadPolicy = RoboFontLoadPolicyStandard;
    }

    JoinPath(manifestPath, sizeof(manifestPath), local.root,
             local.manifestName);
    rc = config->io->RoboReadFile(config->io->userdata, manifestPath,
                                  RoboFontSystemManifestMaxBytes, &bytes, &size);
    if (rc != RoboOk) {
        return rc;
    }
    if (!bytes || size == 0u) {
        RoboFree(bytes);
        return RoboNotFound;
    }

    pack = (FontSystemPack *)RoboCalloc(1u, sizeof(*pack));
    if (!pack) {
        RoboFree(bytes);
        return RoboOutOfMemory;
    }
    pack->registry = registry;
    pack->loader = loader;

    rc = FontSystemManifestParse((const char *)bytes, size, &pack->manifest);
    RoboFree(bytes);
    if (rc != RoboOk) {
        RoboFree(pack);
        return rc;
    }
    {
        const char *root = local.root;
        size_t k = 0u;
        while (root[k] != '\0' && k + 1u < sizeof(pack->root)) {
            pack->root[k] = root[k];
            k++;
        }
        pack->root[k] = '\0';
    }

    FontRegistrySetIo(registry, config->io);

    for (i = 0u; i < pack->manifest.familyCount; i++) {
        FontSystemFamily *fam = &pack->manifest.families[i];
        char path[RoboFontSystemMaxPathBytes];
        FontDiscoverySourceId sid = 0u;
        FontDiscoveryResult scanResult;
        FontFace **faces = NULL;
        uint32_t faceN = 0u;

        JoinPath(path, sizeof(path), pack->root, fam->file);
        rc = FontRegistryAddFileSource(registry, path, &sid);
        if (rc != RoboOk) {
            FontSystemPackClose(pack);
            return rc;
        }
        FontDiscoveryResultInit(&scanResult);
        rc = FontRegistryScanSource(registry, sid, 0u, &scanResult);
        if (rc != RoboOk) {
            FontSystemPackClose(pack);
            return rc;
        }
        if (scanResult.registered == 0u) {
            /* The manifest names a file that carries no usable face. */
            FontSystemPackClose(pack);
            return RoboNotFound;
        }
        rc = FontRegistryCollectFacesForFamily(registry, fam->family, &faces,
                                               &faceN);
        if (rc != RoboOk) {
            FontSystemPackClose(pack);
            return rc;
        }
        if (faceN == 0u) {
            /* The file registered, but not under the declared family. A
             * manifest that lies about its family is a broken pack. */
            RoboFree(faces);
            FontSystemPackClose(pack);
            return RoboNotFound;
        }
        pack->faces[i] = faces;
        pack->faceCount[i] = faceN;
        fam->faceCount = faceN;
        pack->totalFaces += faceN;
        BindVariation(fam, faces[0]);
    }

    if (loader) {
        uint32_t regCount = FontRegistryRegistrationCount(registry);
        uint32_t r;
        for (r = 0u; r < regCount; r++) {
            const FontRegistration *reg = NULL;
            FontLoadResult load;

            if (FontRegistryRegistrationAt(registry, r, &reg) != RoboOk ||
                !reg) {
                continue;
            }
            FontLoadResultInit(&load);
            if (FontLoaderLoadRegistration(loader, reg->id, &load) != RoboOk ||
                load.state != RoboFontLoadLoaded) {
                pack->loadFailures++;
            }
            FontLoadResultClear(&load);
        }
    }

    *out = pack;
    return RoboOk;
}

void FontSystemPackClose(FontSystemPack *pack)
{
    uint32_t i;

    if (!pack) {
        return;
    }
    for (i = 0u; i < RoboFontSystemMaxFamilies; i++) {
        if (pack->faces[i]) {
            RoboFree(pack->faces[i]);
            pack->faces[i] = NULL;
        }
    }
    RoboFree(pack);
}

const char *FontSystemPackRoot(const FontSystemPack *pack)
{
    return pack ? pack->root : NULL;
}

const FontSystemManifest *FontSystemPackManifest(const FontSystemPack *pack)
{
    return pack ? &pack->manifest : NULL;
}

uint32_t FontSystemPackFamilyCount(const FontSystemPack *pack)
{
    return pack ? pack->manifest.familyCount : 0u;
}

const FontSystemFamily *FontSystemPackFamilyAt(const FontSystemPack *pack,
                                                              uint32_t index)
{
    if (!pack || index >= pack->manifest.familyCount) {
        return NULL;
    }
    return &pack->manifest.families[index];
}

uint32_t FontSystemPackRegisteredFaces(const FontSystemPack *pack)
{
    return pack ? pack->totalFaces : 0u;
}

uint32_t FontSystemPackLoadFailures(const FontSystemPack *pack)
{
    return pack ? pack->loadFailures : 0u;
}

Result FontSystemPackRoleBinding(const FontSystemPack *pack,
                                                RoboFontSystemRole role,
                                                const FontSystemFamily **outFamily,
                                                FontWeight *outWeight)
{
    uint32_t i;

    if (!pack || !RoboFontSystemRoleIsValid(role)) {
        return RoboInvalidArgument;
    }
    if (outFamily) {
        *outFamily = NULL;
    }
    if (outWeight) {
        *outWeight = 0u;
    }
    for (i = 0u; i < pack->manifest.familyCount; i++) {
        const FontSystemFamily *fam = &pack->manifest.families[i];
        uint32_t r;

        for (r = 0u; r < fam->roleCount; r++) {
            if (fam->roles[r].role != role) {
                continue;
            }
            if (outFamily) {
                *outFamily = fam;
            }
            if (outWeight) {
                *outWeight = fam->roles[r].weight;
            }
            return RoboOk;
        }
    }
    return RoboNotFound;
}

Result FontSystemPackResolveRole(const FontSystemPack *pack,
                                               RoboFontSystemRole role,
                                               FontWeight weight,
                                               FontStretch stretch,
                                               FontStyle style,
                                               FontSelection *out)
{
    const FontSystemFamily *fam = NULL;
    FontWeight roleWeight = 0u;
    FontSelectRequest req;
    uint32_t index = 0u;

    if (!pack || !out || !RoboFontSystemRoleIsValid(role)) {
        return RoboInvalidArgument;
    }
    if (FontSystemPackRoleBinding(pack, role, &fam, &roleWeight) != RoboOk) {
        return RoboNotFound;
    }
    if (!fam) {
        return RoboNotFound;
    }
    index = (uint32_t)(fam - pack->manifest.families);
    if (index >= pack->manifest.familyCount || !pack->faces[index] ||
        pack->faceCount[index] == 0u) {
        return RoboNotFound;
    }
    {
        FontWeight effective = weight;

        if (effective == 0u) {
            effective = (roleWeight != 0u) ? roleWeight : RoboFontWeightNormal;
        }
        FontSelectRequestInit(&req);
        req.family = fam->family;
        req.weight = effective;
        req.stretch = (stretch != 0u) ? stretch : RoboFontStretchNormal;
        req.style = style;
        if (fam->hasWghtAxis && (float)effective >= fam->wghtMin &&
            (float)effective <= fam->wghtMax &&
            (float)effective != fam->wghtDefault &&
            req.axisCount < RoboFontStyleMaxAxes) {
            req.axes[req.axisCount].tag = RoboFontTagWght;
            req.axes[req.axisCount].value = (float)effective;
            req.axisCount++;
        }
        FontSelectionInit(out);
        return FontSelect(&req, pack->faces[index], pack->faceCount[index],
                          out);
    }
}

Result FontSystemPackResolveCodepoints(
    const FontSystemPack *pack,
    FontFallbackResolver *fallback,
    RoboFontSystemRole role,
    FontWeight weight,
    FontStyle style,
    const uint32_t *codepoints,
    uint32_t codepointCount,
    FontSelection *selectionOut,
    FontFallbackResult *fallbackOut)
{
    FontSelection primary;
    FontFallbackRequest req;
    const FontSystemFamily *fam = NULL;
    FontWeight roleWeight = 0u;
    const char *preferred[1];
    Result rc;

    if (!pack || !fallback || !fallbackOut || !codepoints ||
        codepointCount == 0u || !RoboFontSystemRoleIsValid(role)) {
        return RoboInvalidArgument;
    }
    if (FontSystemPackRoleBinding(pack, role, &fam, &roleWeight) != RoboOk ||
        !fam) {
        return RoboNotFound;
    }

    FontSelectionInit(&primary);
    rc = FontSystemPackResolveRole(pack, role, weight, 0u, style, &primary);
    if (rc != RoboOk) {
        return rc;
    }
    if (!primary.found || !primary.face) {
        FontSelectionClear(&primary);
        return RoboNotFound;
    }

    FontFallbackRequestInit(&req);
    req.primary = primary.face;
    req.codepoints = codepoints;
    req.codepointCount = codepointCount;
    req.weight = (weight != 0u) ? weight
                                : ((roleWeight != 0u) ? roleWeight
                                                      : RoboFontWeightNormal);
    req.stretch = RoboFontStretchNormal;
    req.style = style;
    preferred[0] = fam->family;
    req.preferredFamilies = preferred;
    req.preferredFamilyCount = 1u;

    FontFallbackResultInit(fallbackOut);
    rc = FontFallbackResolve(fallback, &req, fallbackOut);
    if (rc == RoboOk) {
        if (selectionOut) {
            /* Ownership of the retained face moves into the caller's selection
             * (which it initializes with FontSelectionInit). */
            *selectionOut = primary;
        } else {
            FontSelectionClear(&primary);
        }
        return RoboOk;
    }
    /* On failure release the primary exactly once, and leave no dangling
     * retained face in a caller-owned selection. */
    FontSelectionClear(&primary);
    FontFallbackResultClear(fallbackOut);
    if (selectionOut) {
        FontSelectionInit(selectionOut);
    }
    return rc;
}

static Result ApplyMapping(FontGenericConfig *config,
                           FontGenericFamily generic,
                           const char *family)
{
    const char *families[1];

    if (!family || family[0] == '\0') {
        return RoboOk;
    }
    families[0] = family;
    return FontGenericConfigSetMapping(config, generic, families, 1u);
}

Result FontSystemPackApplyGenericConfig(const FontSystemPack *pack,
                                                      FontGenericConfig *config)
{
    const FontSystemFamily *textFam = NULL;
    const FontSystemFamily *monoFam = NULL;
    FontWeight ignored = 0u;
    Result rc;

    if (!pack || !config) {
        return RoboInvalidArgument;
    }
    if (FontSystemPackRoleBinding(pack, RoboFontSystemRoleUIText, &textFam,
                                  &ignored) != RoboOk) {
        return RoboNotFound;
    }
    (void)FontSystemPackRoleBinding(pack, RoboFontSystemRoleMonospace, &monoFam,
                                    &ignored);

    /* system-ui and the sans-serif generics point at the UI Text family. */
    rc = ApplyMapping(config, RoboFontGenericSystemUi, textFam->family);
    if (rc != RoboOk) {
        return rc;
    }
    rc = ApplyMapping(config, RoboFontGenericUiSansSerif, textFam->family);
    if (rc != RoboOk) {
        return rc;
    }
    rc = ApplyMapping(config, RoboFontGenericSansSerif, textFam->family);
    if (rc != RoboOk) {
        return rc;
    }
    if (monoFam) {
        rc = ApplyMapping(config, RoboFontGenericUiMonospace, monoFam->family);
        if (rc != RoboOk) {
            return rc;
        }
        rc = ApplyMapping(config, RoboFontGenericMonospace, monoFam->family);
        if (rc != RoboOk) {
            return rc;
        }
    }
    return RoboOk;
}

Result FontSystemPackFuzzParse(const uint8_t *data, size_t size)
{
    FontSystemManifest scratch;

    FontSystemManifestInit(&scratch);
    if (!data || size == 0u) {
        return RoboOk;
    }
    (void)FontSystemManifestParse((const char *)data, size, &scratch);
    return RoboOk;
}
