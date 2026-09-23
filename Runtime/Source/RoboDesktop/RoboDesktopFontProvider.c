/*
 * RoboDesktop's font provider (Phase 12.17) — see RoboDesktopFontProvider.h.
 *
 * The concrete `LayoutFontProvider`: a family entry in, a borrowed registry face out. Three code paths,
 * one per kind of entry (a generic, a name, a role name), and a miss is always `RoboOk` with a NULL face so
 * the caller's ordered walk continues.
 */

#include "RoboDesktop/RoboDesktopFontProvider.h"

#include "RoboFont/RoboFontGenericFamily.h"
#include "RoboFont/RoboFontSelection.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

struct DesktopFontProvider {
    FontRegistry *registry;
    const FontSystemPack *pack;
    FontGenericResolver *generic;
    uint32_t resolved;
    uint32_t unresolved;
};

/** Case-insensitive ASCII equality, for the two role names the pack speaks. */
static int DesktopNameEqualsCi(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        char ca = *a;
        char cb = *b;

        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

/** The role a generic or a role name asks for, or `RoboFontSystemRoleNone`. */
static RoboFontSystemRole DesktopRoleForGeneric(FontGenericFamily generic)
{
    switch (generic) {
    case RoboFontGenericMonospace:
    case RoboFontGenericUiMonospace:
        return RoboFontSystemRoleMonospace;
    case RoboFontGenericSystemUi:
    case RoboFontGenericSansSerif:
    case RoboFontGenericUiSansSerif:
    case RoboFontGenericSerif:
    case RoboFontGenericUiSerif:
    case RoboFontGenericUiRounded:
    case RoboFontGenericCursive:
    case RoboFontGenericFantasy:
        return RoboFontSystemRoleUIText;
    default:
        return RoboFontSystemRoleNone;
    }
}

/** Select a face for a named family through the registry; borrowed, or NULL when none matches. */
static Result DesktopProviderSelectName(DesktopFontProvider *provider,
                                        const char *name,
                                        const FontSelectRequest *request,
                                        FontFace **outFace)
{
    FontFace **faces = NULL;
    uint32_t faceCount = 0u;
    FontSelection selection;
    FontSelectRequest local;

    *outFace = NULL;
    if (!provider->registry || !name || name[0] == '\0') {
        return RoboOk;
    }
    if (FontRegistryCollectFacesForFamily(provider->registry, name, &faces, &faceCount) != RoboOk) {
        return RoboError;
    }
    local = *request;
    local.family = name;
    local.familyLen = 0u;
    FontSelectionInit(&selection);
    if (FontSelect(&local, faces, faceCount, &selection) == RoboOk && selection.found &&
        selection.face) {
        *outFace = selection.face;
    }
    FontSelectionClear(&selection);
    RoboFree(faces);
    return RoboOk;
}

/** Select a face for a pack role; borrowed, or NULL. */
static Result DesktopProviderSelectRole(DesktopFontProvider *provider,
                                        RoboFontSystemRole role,
                                        const FontSelectRequest *request,
                                        FontFace **outFace)
{
    FontSelection selection;

    *outFace = NULL;
    if (!provider->pack || role == RoboFontSystemRoleNone) {
        return RoboOk;
    }
    FontSelectionInit(&selection);
    if (FontSystemPackResolveRole(provider->pack, role, request->weight, request->stretch,
                                  request->style, &selection) == RoboOk &&
        selection.found && selection.face) {
        *outFace = selection.face;
    }
    FontSelectionClear(&selection);
    return RoboOk;
}

/** Select a face for a generic, through the pack's configured mappings then the pack's roles. */
static Result DesktopProviderSelectGeneric(DesktopFontProvider *provider,
                                           FontGenericFamily generic,
                                           const FontSelectRequest *request,
                                           FontFace **outFace)
{
    FontGenericRequest genericRequest;
    FontGenericResult genericResult;
    FontSelection selection;

    *outFace = NULL;
    if (generic == RoboFontGenericNone) {
        return RoboOk;
    }
    if (provider->generic) {
        FontGenericRequestInit(&genericRequest);
        genericRequest.generic = generic;
        FontGenericResultInit(&genericResult);
        FontSelectionInit(&selection);
        if (FontGenericResolveSelect(provider->generic, &genericRequest, request, &genericResult,
                                     &selection) == RoboOk &&
            selection.found && selection.face) {
            *outFace = selection.face;
        }
        FontSelectionClear(&selection);
        FontGenericResultClear(&genericResult);
        if (*outFace) {
            return RoboOk;
        }
    }
    /* No configured mapping answered (or there is none): the pack's own roles are the fallback, so
     * `system-ui` still resolves when the generic layer is empty. */
    return DesktopProviderSelectRole(provider, DesktopRoleForGeneric(generic), request, outFace);
}

static Result DesktopProviderResolveFamily(void *userdata,
                                           const LayoutFontFamily *family,
                                           FontWeight weight,
                                           FontStretch stretch,
                                           FontStyle style,
                                           FontFace **outFace)
{
    DesktopFontProvider *provider = (DesktopFontProvider *)userdata;
    FontSelectRequest request;
    Result status;

    if (!provider || !family || !outFace) {
        return RoboInvalidArgument;
    }
    *outFace = NULL;
    FontSelectRequestInit(&request);
    request.weight = weight;
    request.stretch = stretch;
    request.style = style;

    if (family->kind == LayoutFontFamilyGeneric) {
        status = DesktopProviderSelectGeneric(provider, family->generic, &request, outFace);
    } else if (DesktopNameEqualsCi(family->name, "ui-display")) {
        status = DesktopProviderSelectRole(provider, RoboFontSystemRoleUIDisplay, &request, outFace);
    } else if (DesktopNameEqualsCi(family->name, "ui-text")) {
        status = DesktopProviderSelectRole(provider, RoboFontSystemRoleUIText, &request, outFace);
    } else {
        status = DesktopProviderSelectName(provider, family->name, &request, outFace);
    }
    if (status != RoboOk) {
        return status;
    }
    if (*outFace) {
        provider->resolved++;
    } else {
        provider->unresolved++;
    }
    return RoboOk;
}

Result DesktopFontProviderCreate(FontRegistry *registry,
                                 const FontSystemPack *pack,
                                 DesktopFontProvider **out,
                                 DesktopDiag *diag)
{
    DesktopFontProvider *provider;

    if (!registry || !out) {
        return RoboDesktopDiag(diag, RoboDesktopDomainDocument, RoboInvalidArgument,
                               "a font provider needs a registry and an out pointer");
    }
    *out = NULL;
    provider = (DesktopFontProvider *)RoboCalloc(1u, sizeof(*provider));
    if (!provider) {
        return RoboDesktopDiag(diag, RoboDesktopDomainDocument, RoboOutOfMemory,
                               "the font provider could not be allocated");
    }
    provider->registry = registry;
    provider->pack = pack;

    if (pack) {
        FontGenericConfig *config = NULL;
        FontGenericResolverOptions options;

        if (FontGenericConfigCreate(&config) != RoboOk) {
            RoboFree(provider);
            return RoboDesktopDiag(diag, RoboDesktopDomainDocument, RoboOutOfMemory,
                                   "the generic-font configuration could not be created");
        }
        (void)FontSystemPackApplyGenericConfig(pack, config);
        FontGenericResolverOptionsInit(&options);
        if (FontGenericResolverCreate(registry, &options, &provider->generic) != RoboOk) {
            FontGenericConfigRelease(config);
            RoboFree(provider);
            return RoboDesktopDiag(diag, RoboDesktopDomainDocument, RoboOutOfMemory,
                                   "the generic-font resolver could not be created");
        }
        /* The resolver **retains** the config, so this reference is the caller's to drop. */
        (void)FontGenericResolverSetApplication(provider->generic, config);
        FontGenericConfigRelease(config);
    }

    *out = provider;
    return RoboOk;
}

void DesktopFontProviderDestroy(DesktopFontProvider *provider)
{
    if (!provider) {
        return;
    }
    if (provider->generic) {
        FontGenericResolverDestroy(provider->generic);
        provider->generic = NULL;
    }
    RoboFree(provider);
}

LayoutFontProvider DesktopFontProviderLayout(const DesktopFontProvider *provider)
{
    LayoutFontProvider layout;

    LayoutFontProviderInit(&layout);
    if (provider) {
        layout.userdata = (void *)provider;
        layout.resolveFamily = DesktopProviderResolveFamily;
    }
    return layout;
}

uint32_t DesktopFontProviderResolved(const DesktopFontProvider *provider)
{
    return provider ? provider->resolved : 0u;
}

uint32_t DesktopFontProviderUnresolved(const DesktopFontProvider *provider)
{
    return provider ? provider->unresolved : 0u;
}
