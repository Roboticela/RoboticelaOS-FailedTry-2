/*
 * RoboCSS container queries (Phase 18.11) — see RoboCSSContainer.h.
 *
 * One table for `container-type` and one matcher over the media-query module's
 * condition model. Nothing here measures a container or walks an ancestor chain.
 */

#include "RoboCSS/RoboCSSContainer.h"

#include "RoboCSSInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Shared helpers                                                      */
/* ------------------------------------------------------------------ */

static Result ContainerWriteKeyword(const char *keyword,
                                    char *buffer,
                                    size_t capacity,
                                    size_t *outLength,
                                    CssDiag *diag)
{
    size_t length;

    if (!buffer || !outLength || capacity == 0u) {
        return RoboCssDiag(diag, RoboCssDomainContainer, RoboInvalidArgument,
                           "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *outLength = 0u;
    if (!keyword) {
        return RoboCssDiag(diag, RoboCssDomainContainer, RoboInvalidArgument,
                           "not a container value");
    }
    length = strlen(keyword);
    if (length >= capacity) {
        *outLength = length;
        return RoboCssDiag(diag, RoboCssDomainContainer, RoboNotSupported,
                           "the container keyword does not fit");
    }
    memcpy(buffer, keyword, length);
    buffer[length] = '\0';
    *outLength = length;
    return RoboOk;
}

static int ContainerKeywordEquals(CssSlice keyword, const char *spelling)
{
    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    return CssBytesEqualAsciiCi(keyword.bytes, keyword.length, spelling);
}

/* ------------------------------------------------------------------ */
/* container-type                                                      */
/* ------------------------------------------------------------------ */

typedef struct containerTypeRow {
    CssContainerType value;
    const char *keyword;
    int establishes;
    int inlineOnly;
} ContainerTypeRow;

static const ContainerTypeRow containerTypeVocabulary[] = {
    { RoboCssContainerTypeNormal,     "normal",      0, 0 },
    { RoboCssContainerTypeSize,       "size",        1, 0 },
    { RoboCssContainerTypeInlineSize, "inline-size", 1, 1 }
};

#define CONTAINER_TYPE_COUNT \
    (sizeof(containerTypeVocabulary) / sizeof(containerTypeVocabulary[0]))

static const ContainerTypeRow *ContainerTypeRowOf(CssContainerType type)
{
    size_t index;

    for (index = 0u; index < CONTAINER_TYPE_COUNT; index++) {
        if (containerTypeVocabulary[index].value == type) {
            return &containerTypeVocabulary[index];
        }
    }
    return NULL;
}

int CssContainerTypeIsKnown(CssContainerType type)
{
    return ContainerTypeRowOf(type) != NULL;
}

const char *CssContainerTypeName(CssContainerType type)
{
    const ContainerTypeRow *row = ContainerTypeRowOf(type);

    return row ? row->keyword : "";
}

size_t CssContainerTypeCount(void)
{
    return CONTAINER_TYPE_COUNT;
}

CssContainerType CssContainerTypeFromSlice(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return RoboCssContainerTypeNormal;
    }
    for (index = 0u; index < CONTAINER_TYPE_COUNT; index++) {
        if (ContainerKeywordEquals(keyword,
                                   containerTypeVocabulary[index].keyword)) {
            return containerTypeVocabulary[index].value;
        }
    }
    return RoboCssContainerTypeNormal;
}

CssContainerType CssContainerTypeFromText(const char *keyword)
{
    if (!keyword) {
        return RoboCssContainerTypeNormal;
    }
    return CssContainerTypeFromSlice(
        CssSliceFromBytes((const uint8_t *)keyword, strlen(keyword)));
}

int CssContainerTypeSliceIsKnown(CssSlice keyword)
{
    size_t index;

    if (CssSliceIsAbsent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < CONTAINER_TYPE_COUNT; index++) {
        if (ContainerKeywordEquals(keyword,
                                   containerTypeVocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

int CssContainerTypeEstablishes(CssContainerType type)
{
    const ContainerTypeRow *row = ContainerTypeRowOf(type);

    return row ? row->establishes : 0;
}

int CssContainerTypeIsInlineOnly(CssContainerType type)
{
    const ContainerTypeRow *row = ContainerTypeRowOf(type);

    return row ? row->inlineOnly : 0;
}

Result CssContainerTypeWrite(CssContainerType type,
                             char *buffer,
                             size_t capacity,
                             size_t *outLength,
                             CssDiag *diag)
{
    const ContainerTypeRow *row = ContainerTypeRowOf(type);

    if (!row) {
        if (buffer && outLength && capacity > 0u) {
            buffer[0] = '\0';
        }
        return RoboCssDiag(diag, RoboCssDomainContainer, RoboInvalidArgument,
                           "not a container-type value");
    }
    return ContainerWriteKeyword(row->keyword, buffer, capacity, outLength, diag);
}

/* ------------------------------------------------------------------ */
/* matching                                                            */
/* ------------------------------------------------------------------ */

CssMediaMatch CssContainerQueryMatches(const CssMediaCondition *conditions,
                                       size_t conditionCount,
                                       CssContainerType containerType,
                                       const CssMediaContext *size)
{
    CssMediaMatch overall = RoboCssMediaMatchYes;
    size_t index;

    if (!size) {
        return RoboCssMediaMatchUnknown;
    }
    /* A box that never declared itself a query container answers nothing. */
    if (!CssContainerTypeEstablishes(containerType)) {
        return RoboCssMediaMatchUnknown;
    }

    for (index = 0u;
         overall != RoboCssMediaMatchNo && index < conditionCount; index++) {
        CssMediaMatch match = CssMediaConditionMatches(&conditions[index], size);

        if (match == RoboCssMediaMatchNo) {
            overall = RoboCssMediaMatchNo;
        } else if (match == RoboCssMediaMatchUnknown) {
            overall = RoboCssMediaMatchUnknown;
        }
    }
    return overall;
}
