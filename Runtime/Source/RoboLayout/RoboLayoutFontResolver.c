/*
 * RoboLayout's font selection seam (Phase 12.17) — see RoboLayoutFontResolver.h.
 *
 * The provider vtable and the ordered walk over a request's families. Nothing here knows a family name,
 * a generic mapping or a directory: a generic is handed to the provider as a `FontGenericFamily` and the
 * provider resolves it.
 */

#include "RoboLayout/RoboLayoutFontResolver.h"

#include "RoboFont/RoboFontGenericFamily.h"

#include <stdio.h>
#include <string.h>

void LayoutFontProviderInit(LayoutFontProvider *provider)
{
    if (!provider) {
        return;
    }
    provider->userdata = NULL;
    provider->resolveFamily = NULL;
}

int LayoutFontProviderIsUsable(const LayoutFontProvider *provider)
{
    return provider != NULL && provider->resolveFamily != NULL;
}

void LayoutFontSelectionInit(LayoutFontSelection *selection)
{
    if (!selection) {
        return;
    }
    memset(selection, 0, sizeof(*selection));
    selection->weight = RoboFontWeightNormal;
    selection->stretch = RoboFontStretchNormal;
    selection->style = RoboFontStyleNormal;
    selection->familyIndex = RoboLayoutFontNoFamily;
    selection->fallback = 0;
    selection->resolved = 0;
}

int LayoutFontSelectionIsSet(const LayoutFontSelection *selection)
{
    return selection != NULL && selection->resolved != 0 && selection->face != NULL;
}

int LayoutFontSelectionEqual(const LayoutFontSelection *a, const LayoutFontSelection *b)
{
    if (!a || !b) {
        return !a && !b;
    }
    if (a->face != b->face || a->resolved != b->resolved ||
        a->fallback != b->fallback || a->familyIndex != b->familyIndex) {
        return 0;
    }
    return a->weight == b->weight && a->stretch == b->stretch && a->style == b->style;
}

static int LayoutFontSelectionAppend(char *buffer,
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

Result LayoutFontSelectionWrite(const LayoutFontSelection *selection,
                                char *buffer,
                                size_t capacity,
                                size_t *outLength)
{
    size_t cursor = 0u;
    char scalars[128];

    if (!selection || !buffer || capacity == 0u) {
        return RoboInvalidArgument;
    }
    buffer[0] = '\0';

    if (!LayoutFontSelectionAppend(buffer, capacity, &cursor, "face=")) {
        return RoboNotSupported;
    }
    if (!selection->resolved || !selection->face) {
        if (!LayoutFontSelectionAppend(buffer, capacity, &cursor, "none")) {
            return RoboNotSupported;
        }
    } else {
        (void)snprintf(scalars, sizeof(scalars), "%p family[%lu]%s",
                       (const void *)selection->face,
                       (unsigned long)selection->familyIndex,
                       selection->fallback ? " fallback" : "");
        if (!LayoutFontSelectionAppend(buffer, capacity, &cursor, scalars)) {
            return RoboNotSupported;
        }
    }

    (void)snprintf(scalars, sizeof(scalars), " weight=%u stretch=%u style=%d",
                   (unsigned)selection->weight, (unsigned)selection->stretch,
                   (int)selection->style);
    if (!LayoutFontSelectionAppend(buffer, capacity, &cursor, scalars)) {
        return RoboNotSupported;
    }
    if (outLength) {
        *outLength = cursor;
    }
    return RoboOk;
}

Result LayoutFontRequestSelect(const LayoutFontRequest *request,
                               const LayoutFontProvider *provider,
                               LayoutFontSelection *out,
                               LayoutDiag *diag)
{
    uint32_t index;

    if (!request || !out) {
        return RoboInvalidArgument;
    }
    LayoutFontSelectionInit(out);

    out->weight = (FontWeight)request->weight;
    out->stretch = request->stretch;
    out->style = request->italic ? RoboFontStyleItalic : RoboFontStyleNormal;

    if (!LayoutFontProviderIsUsable(provider)) {
        /* No database to consult: an honest unset selection rather than a failure. */
        return RoboOk;
    }

    /*
     * The requested families, in author order. A generic is passed through **as a generic** (its name is
     * empty and its `generic` field carries the keyword), so the provider consults the configured mapping
     * rather than this module inventing one.
     */
    for (index = 0u; index < request->families.count; index++) {
        FontFace *face = NULL;
        Result status = provider->resolveFamily(provider->userdata,
                                                &request->families.entries[index],
                                                out->weight, out->stretch, out->style, &face);

        if (status != RoboOk) {
            return status;
        }
        if (face) {
            out->face = face;
            out->resolved = 1;
            out->familyIndex = index;
            out->fallback = index > 0u ? 1 : 0;
            return RoboOk;
        }
    }
    (void)diag;
    return RoboOk;
}
