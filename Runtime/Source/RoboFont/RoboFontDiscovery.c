/*
 * RoboFont discovery + registry implementation (Goal 5.12).
 */

#include "RoboFont/RoboFontDiscovery.h"
#include "RoboFont/RoboFontName.h"
#include "RoboFont/RoboFontSource.h"
#include "RoboUnicode/RoboUnicodeCase.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

/* ---- Limits (internal virtual tree) ---- */
#define VIO_MAX_NODES 512u
#define VIO_MAX_NAME 256u

typedef struct discSource {
    FontDiscoverySourceId id;
    FontDiscoverySourceKind kind;
    char path[RoboFontDiscoveryMaxPath];
    int recursive;
    /* MEMORY only */
    FontSource *memSource;
    uint8_t active;
} DiscSource;

struct FontRegistry {
    uint64_t nextRegId;
    uint64_t nextSrcId;
    uint32_t generation;

    DiscSource sources[RoboFontRegistryMaxSources];
    uint32_t sourceCount;

    FontRegistration *regs;
    uint32_t regCount;
    uint32_t regCap;

    const FontDiscoveryIo *io;

    FontRegistryInvalidateFn InvalidateFn;
    void *invalidateUd;
};

/* ---- Helpers ---- */

static void CopyStr(char *dst, size_t cap, const char *src)
{
    size_t i;
    if (!dst || cap == 0u) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    for (i = 0u; i + 1u < cap && src[i]; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static int PathLenOk(const char *path)
{
    size_t n;
    if (!path || !path[0]) {
        return 0;
    }
    n = strlen(path);
    return n > 0u && n < RoboFontDiscoveryMaxPath;
}

static int Utf8CasefoldEq(const char *a, const char *b)
{
    int cmp = 1;
    size_t al, bl;
    if (!a || !b) {
        return 0;
    }
    al = strlen(a);
    bl = strlen(b);
    if (UnicodeCasefoldCompareUtf8(a, al, b, bl, RoboUnicodeReplace, &cmp) !=
        RoboOk) {
        return 0;
    }
    return cmp == 0;
}

static void SnapshotField(char *dst, size_t cap, const FontMetaField *f)
{
    dst[0] = '\0';
    if (!f || !f->utf8 || f->utf8Len == 0u) {
        return;
    }
    if (f->avail != RoboFontNamePresent && f->avail != RoboFontNameFallback) {
        return;
    }
    if (f->utf8Len >= cap) {
        memcpy(dst, f->utf8, cap - 1u);
        dst[cap - 1u] = '\0';
    } else {
        memcpy(dst, f->utf8, f->utf8Len);
        dst[f->utf8Len] = '\0';
    }
}

void FontDiscoveryResultInit(FontDiscoveryResult *r)
{
    if (!r) {
        return;
    }
    memset(r, 0, sizeof(*r));
    r->lastError = RoboOk;
}

FontCandidateFormat FontDiscoveryClassifyBytes(const uint8_t *data,
                                                               size_t size)
{
    uint32_t tag;
    if (!data || size < 4u) {
        return RoboFontCandidateUnknown;
    }
    tag = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
          ((uint32_t)data[2] << 8) | (uint32_t)data[3];
    if (tag == RoboFontSfntVersionTtcf) {
        return RoboFontCandidateCollection;
    }
    if (tag == RoboFontSfntVersionOtto) {
        return RoboFontCandidateOtf;
    }
    if (tag == RoboFontSfntVersionTrue || tag == RoboFontSfntVersionTrueTag ||
        tag == RoboFontSfntVersionTyp1) {
        return RoboFontCandidateTtf;
    }
    return RoboFontCandidateUnsupported;
}

int FontDiscoveryExtensionIsFont(const char *pathOrName)
{
    const char *dot;
    char ext[8];
    size_t i, n;
    if (!pathOrName) {
        return 0;
    }
    dot = strrchr(pathOrName, '.');
    if (!dot || !dot[1]) {
        return 0;
    }
    n = strlen(dot + 1);
    if (n == 0u || n >= sizeof(ext)) {
        return 0;
    }
    for (i = 0u; i < n; i++) {
        char c = dot[1 + i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        ext[i] = c;
    }
    ext[n] = '\0';
    return strcmp(ext, "ttf") == 0 || strcmp(ext, "otf") == 0 ||
           strcmp(ext, "ttc") == 0 || strcmp(ext, "otc") == 0;
}

/* ---- Stub IO ---- */

static Result StubRead(void *ud, const char *path, size_t maxSize,
                               uint8_t **outBytes, size_t *outSize)
{
    (void)ud;
    (void)path;
    (void)maxSize;
    if (outBytes) {
        *outBytes = NULL;
    }
    if (outSize) {
        *outSize = 0u;
    }
    return RoboNotSupported;
}

static Result StubList(void *ud, const char *path,
                               int (*visit)(void *, const char *, int), void *visitUd)
{
    (void)ud;
    (void)path;
    (void)visit;
    (void)visitUd;
    return RoboNotSupported;
}

static int StubIsDir(void *ud, const char *path)
{
    (void)ud;
    (void)path;
    return 0;
}

static int StubIsFile(void *ud, const char *path)
{
    (void)ud;
    (void)path;
    return 0;
}

static const FontDiscoveryIo gStubIo = {
    NULL, StubRead, StubList, StubIsDir, StubIsFile};

const FontDiscoveryIo *FontDiscoveryIoStub(void)
{
    return &gStubIo;
}

/* ---- Virtual IO ---- */

typedef struct vioNode {
    char name[VIO_MAX_NAME];
    int isDir;
    uint8_t *bytes; /* file only; owned */
    size_t size;
    uint32_t parent; /* index; 0 = root */
    uint8_t used;
} VioNode;

struct FontDiscoveryVirtualIo {
    VioNode nodes[VIO_MAX_NODES];
    uint32_t nodeCount; /* includes root at 0 */
    FontDiscoveryIo ops;
};

static uint32_t VioFindChild(const FontDiscoveryVirtualIo *vio,
                               uint32_t parent,
                               const char *name)
{
    uint32_t i;
    for (i = 1u; i < vio->nodeCount; i++) {
        if (vio->nodes[i].used && vio->nodes[i].parent == parent &&
            strcmp(vio->nodes[i].name, name) == 0) {
            return i;
        }
    }
    return 0u;
}

static Result VioEnsurePath(FontDiscoveryVirtualIo *vio,
                                     const char *path,
                                     int asDir,
                                     uint32_t *outNode)
{
    uint32_t cur = 0u;
    const char *p;
    char part[VIO_MAX_NAME];
    size_t pi;

    if (!vio || !path || path[0] != '/') {
        return RoboInvalidArgument;
    }
    p = path + 1;
    if (*p == '\0') {
        if (outNode) {
            *outNode = 0u;
        }
        return RoboOk;
    }
    while (*p) {
        int last;
        pi = 0u;
        while (*p && *p != '/') {
            if (pi + 1u >= sizeof(part)) {
                return RoboInvalidArgument;
            }
            part[pi++] = *p++;
        }
        part[pi] = '\0';
        if (pi == 0u) {
            return RoboInvalidArgument;
        }
        last = (*p == '\0');
        if (*p == '/') {
            p++;
        }
        {
            uint32_t child = VioFindChild(vio, cur, part);
            if (child == 0u) {
                if (vio->nodeCount >= VIO_MAX_NODES) {
                    return RoboOutOfMemory;
                }
                child = vio->nodeCount++;
                memset(&vio->nodes[child], 0, sizeof(vio->nodes[child]));
                vio->nodes[child].used = 1u;
                vio->nodes[child].parent = cur;
                CopyStr(vio->nodes[child].name, sizeof(vio->nodes[child].name), part);
                vio->nodes[child].isDir = (!last || asDir) ? 1 : 0;
            }
            if (last && !asDir) {
                if (vio->nodes[child].isDir && vio->nodes[child].bytes == NULL) {
                    /* converting empty dir to file is ok if no children — skip check */
                }
                vio->nodes[child].isDir = 0;
            }
            if (!last && !vio->nodes[child].isDir) {
                return RoboInvalidArgument;
            }
            cur = child;
        }
    }
    if (outNode) {
        *outNode = cur;
    }
    return RoboOk;
}

static int VioLookup(const FontDiscoveryVirtualIo *vio,
                      const char *path,
                      uint32_t *outNode)
{
    uint32_t cur = 0u;
    const char *p;
    char part[VIO_MAX_NAME];
    size_t pi;

    if (!vio || !path || path[0] != '/' || !outNode) {
        return 0;
    }
    *outNode = 0u;
    p = path + 1;
    if (*p == '\0') {
        *outNode = 0u;
        return 1;
    }
    while (*p) {
        uint32_t child;
        pi = 0u;
        while (*p && *p != '/') {
            if (pi + 1u >= sizeof(part)) {
                return 0;
            }
            part[pi++] = *p++;
        }
        part[pi] = '\0';
        if (pi == 0u) {
            return 0;
        }
        if (*p == '/') {
            p++;
        }
        child = VioFindChild(vio, cur, part);
        if (child == 0u) {
            return 0;
        }
        cur = child;
    }
    *outNode = cur;
    return 1;
}

static Result VioRead(void *userdata, const char *path, size_t maxSize,
                              uint8_t **outBytes, size_t *outSize)
{
    FontDiscoveryVirtualIo *vio = (FontDiscoveryVirtualIo *)userdata;
    uint32_t node = 0u;
    uint8_t *copy;

    if (!vio || !outBytes || !outSize) {
        return RoboInvalidArgument;
    }
    *outBytes = NULL;
    *outSize = 0u;
    if (!VioLookup(vio, path, &node) || node == 0u || vio->nodes[node].isDir ||
        !vio->nodes[node].bytes) {
        return RoboNotFound;
    }
    if (vio->nodes[node].size > maxSize) {
        return RoboParseError;
    }
    copy = (uint8_t *)Malloc(vio->nodes[node].size ? vio->nodes[node].size : 1u);
    if (!copy) {
        return RoboOutOfMemory;
    }
    if (vio->nodes[node].size > 0u) {
        memcpy(copy, vio->nodes[node].bytes, vio->nodes[node].size);
    }
    *outBytes = copy;
    *outSize = vio->nodes[node].size;
    return RoboOk;
}

static Result VioList(void *userdata, const char *path,
                              int (*visit)(void *, const char *, int), void *visitUd)
{
    FontDiscoveryVirtualIo *vio = (FontDiscoveryVirtualIo *)userdata;
    uint32_t parent = 0u;
    uint32_t i;

    if (!vio || !visit) {
        return RoboInvalidArgument;
    }
    if (!path || strcmp(path, "/") == 0) {
        parent = 0u;
    } else if (!VioLookup(vio, path, &parent) || !vio->nodes[parent].isDir) {
        return RoboNotFound;
    }
    for (i = 1u; i < vio->nodeCount; i++) {
        if (vio->nodes[i].used && vio->nodes[i].parent == parent) {
            if (visit(visitUd, vio->nodes[i].name, vio->nodes[i].isDir)) {
                break;
            }
        }
    }
    return RoboOk;
}

static int VioIsDir(void *userdata, const char *path)
{
    FontDiscoveryVirtualIo *vio = (FontDiscoveryVirtualIo *)userdata;
    uint32_t node = 0u;
    if (!vio || !path) {
        return 0;
    }
    if (!VioLookup(vio, path, &node)) {
        return 0;
    }
    return node == 0u || vio->nodes[node].isDir;
}

static int VioIsFile(void *userdata, const char *path)
{
    FontDiscoveryVirtualIo *vio = (FontDiscoveryVirtualIo *)userdata;
    uint32_t node = 0u;
    if (!vio || !path) {
        return 0;
    }
    if (!VioLookup(vio, path, &node) || node == 0u) {
        return 0;
    }
    return !vio->nodes[node].isDir && vio->nodes[node].bytes != NULL;
}

Result FontDiscoveryVirtualIoCreate(
    FontDiscoveryVirtualIo **out)
{
    FontDiscoveryVirtualIo *vio;
    if (!out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    vio = (FontDiscoveryVirtualIo *)RoboCalloc(1, sizeof(*vio));
    if (!vio) {
        return RoboOutOfMemory;
    }
    vio->nodeCount = 1u;
    vio->nodes[0].used = 1u;
    vio->nodes[0].isDir = 1;
    vio->nodes[0].name[0] = '\0';
    vio->ops.userdata = vio;
    vio->ops.RoboReadFile = VioRead;
    vio->ops.ListDir = VioList;
    vio->ops.IsDirectory = VioIsDir;
    vio->ops.isFile = VioIsFile;
    *out = vio;
    return RoboOk;
}

void FontDiscoveryVirtualIoDestroy(FontDiscoveryVirtualIo *vio)
{
    uint32_t i;
    if (!vio) {
        return;
    }
    for (i = 0u; i < vio->nodeCount; i++) {
        if (vio->nodes[i].bytes) {
            RoboFree(vio->nodes[i].bytes);
            vio->nodes[i].bytes = NULL;
        }
    }
    RoboFree(vio);
}

const FontDiscoveryIo *FontDiscoveryVirtualIoOps(
    FontDiscoveryVirtualIo *vio)
{
    return vio ? &vio->ops : NULL;
}

Result FontDiscoveryVirtualAddFile(
    FontDiscoveryVirtualIo *vio,
    const char *path,
    const void *bytes,
    size_t size)
{
    uint32_t node = 0u;
    uint8_t *copy;
    Result res;

    if (!vio || !path || !bytes || size == 0u) {
        return RoboInvalidArgument;
    }
    if (size > RoboFontDiscoveryMaxFileSize) {
        return RoboParseError;
    }
    res = VioEnsurePath(vio, path, 0, &node);
    if (res != RoboOk) {
        return res;
    }
    if (node == 0u) {
        return RoboInvalidArgument;
    }
    copy = (uint8_t *)Malloc(size);
    if (!copy) {
        return RoboOutOfMemory;
    }
    memcpy(copy, bytes, size);
    if (vio->nodes[node].bytes) {
        RoboFree(vio->nodes[node].bytes);
    }
    vio->nodes[node].bytes = copy;
    vio->nodes[node].size = size;
    vio->nodes[node].isDir = 0;
    return RoboOk;
}

Result FontDiscoveryVirtualAddDir(
    FontDiscoveryVirtualIo *vio,
    const char *path)
{
    uint32_t node = 0u;
    if (!vio || !path) {
        return RoboInvalidArgument;
    }
    return VioEnsurePath(vio, path, 1, &node);
}

/* ---- Registry core ---- */

static void NotifyInvalidate(FontRegistry *reg,
                              FontDiscoverySourceId sourceId,
                              FontRegistrationId registrationId)
{
    if (reg && reg->InvalidateFn) {
        reg->InvalidateFn(reg->invalidateUd, sourceId, registrationId);
    }
}

void FontRegistrySetInvalidateHook(FontRegistry *reg,
                                            FontRegistryInvalidateFn fn,
                                            void *userdata)
{
    if (!reg) {
        return;
    }
    reg->InvalidateFn = fn;
    reg->invalidateUd = userdata;
}

uint64_t FontRegistryGeneration(const FontRegistry *reg)
{
    return reg ? reg->generation : 0u;
}

Result FontRegistryCreate(FontRegistry **out)
{
    FontRegistry *reg;
    if (!out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    reg = (FontRegistry *)RoboCalloc(1, sizeof(*reg));
    if (!reg) {
        return RoboOutOfMemory;
    }
    reg->nextRegId = 1u;
    reg->nextSrcId = 1u;
    reg->io = FontDiscoveryIoStub();
    *out = reg;
    return RoboOk;
}

static void FreeRegistrationAt(FontRegistry *reg, uint32_t i)
{
    if (reg->regs[i].face) {
        FontFaceRelease(reg->regs[i].face);
        reg->regs[i].face = NULL;
    }
}

void FontRegistryClear(FontRegistry *reg)
{
    uint32_t i;
    if (!reg) {
        return;
    }
    for (i = 0u; i < reg->regCount; i++) {
        FreeRegistrationAt(reg, i);
    }
    reg->regCount = 0u;
    reg->generation++;
    NotifyInvalidate(reg, 0u, 0u);
}

void FontRegistryDestroy(FontRegistry *reg)
{
    uint32_t i;
    if (!reg) {
        return;
    }
    FontRegistryClear(reg);
    for (i = 0u; i < reg->sourceCount; i++) {
        if (reg->sources[i].memSource) {
            FontSourceRelease(reg->sources[i].memSource);
            reg->sources[i].memSource = NULL;
        }
    }
    if (reg->regs) {
        RoboFree(reg->regs);
    }
    RoboFree(reg);
}

uint32_t FontRegistryRegistrationCount(const FontRegistry *reg)
{
    return reg ? reg->regCount : 0u;
}

uint32_t FontRegistrySourceCount(const FontRegistry *reg)
{
    uint32_t i, n = 0u;
    if (!reg) {
        return 0u;
    }
    for (i = 0u; i < reg->sourceCount; i++) {
        if (reg->sources[i].active) {
            n++;
        }
    }
    return n;
}

void FontRegistrySetIo(FontRegistry *reg,
                               const FontDiscoveryIo *io)
{
    if (!reg) {
        return;
    }
    reg->io = io ? io : FontDiscoveryIoStub();
}

Result FontRegistryRegistrationAt(const FontRegistry *reg,
                                                 uint32_t index,
                                                 const FontRegistration **out)
{
    if (!reg || !out) {
        return RoboInvalidArgument;
    }
    if (index >= reg->regCount) {
        return RoboNotFound;
    }
    *out = &reg->regs[index];
    return RoboOk;
}

Result FontRegistryFindById(const FontRegistry *reg,
                                            FontRegistrationId id,
                                            const FontRegistration **out)
{
    uint32_t i;
    if (!reg || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    for (i = 0u; i < reg->regCount; i++) {
        if (reg->regs[i].id == id) {
            *out = &reg->regs[i];
            return RoboOk;
        }
    }
    return RoboNotFound;
}

Result FontRegistryFindByFaceId(const FontRegistry *reg,
                                                 FontFaceId faceId,
                                                 const FontRegistration **out)
{
    uint32_t i;
    if (!reg || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    for (i = 0u; i < reg->regCount; i++) {
        if (reg->regs[i].faceId == faceId) {
            *out = &reg->regs[i];
            return RoboOk;
        }
    }
    return RoboNotFound;
}

static Result EnsureRegCap(FontRegistry *reg, uint32_t need)
{
    FontRegistration *nregs;
    uint32_t ncap;
    if (need <= reg->regCap) {
        return RoboOk;
    }
    if (need > RoboFontRegistryMaxRegistrations) {
        return RoboOutOfMemory;
    }
    ncap = reg->regCap == 0u ? 16u : reg->regCap * 2u;
    while (ncap < need) {
        ncap *= 2u;
    }
    if (ncap > RoboFontRegistryMaxRegistrations) {
        ncap = RoboFontRegistryMaxRegistrations;
    }
    nregs = (FontRegistration *)RoboCalloc(ncap, sizeof(*nregs));
    if (!nregs) {
        return RoboOutOfMemory;
    }
    if (reg->regs && reg->regCount > 0u) {
        memcpy(nregs, reg->regs, (size_t)reg->regCount * sizeof(*nregs));
        RoboFree(reg->regs);
    }
    reg->regs = nregs;
    reg->regCap = ncap;
    return RoboOk;
}

static int ContentDuplicate(const FontRegistry *reg,
                             const char *psName,
                             const FontStyleDesc *style)
{
    uint32_t i;
    if (!psName || !psName[0] || !style) {
        return 0;
    }
    for (i = 0u; i < reg->regCount; i++) {
        if (reg->regs[i].postscriptName[0] &&
            Utf8CasefoldEq(reg->regs[i].postscriptName, psName) &&
            reg->regs[i].style.weight == style->weight &&
            reg->regs[i].style.stretch == style->stretch &&
            reg->regs[i].style.style == style->style) {
            return 1;
        }
    }
    return 0;
}

static int SourceIndexRegistered(const FontRegistry *reg,
                                   FontDiscoverySourceId sourceId,
                                   uint32_t faceIndex,
                                   const char *origin)
{
    uint32_t i;
    const char *o = origin ? origin : "";
    for (i = 0u; i < reg->regCount; i++) {
        if (reg->regs[i].sourceId == sourceId &&
            reg->regs[i].faceIndex == faceIndex &&
            strcmp(reg->regs[i].origin, o) == 0) {
            return 1;
        }
    }
    return 0;
}

static Result RegisterFace(FontRegistry *reg,
                                   FontDiscoverySourceId sourceId,
                                   uint32_t faceIndex,
                                   uint8_t isCollection,
                                   FontFace *face,
                                   uint32_t flags,
                                   const char *origin,
                                   int *outDup)
{
    const FontMetadata *meta;
    const FontClassification *cls;
    FontRegistration *slot;
    Result res;
    char family[128];
    char ps[128];

    if (outDup) {
        *outDup = 0;
    }
    if (!reg || !face) {
        return RoboInvalidArgument;
    }
    if (SourceIndexRegistered(reg, sourceId, faceIndex, origin)) {
        if (outDup) {
            *outDup = 1;
        }
        return RoboAlreadyExists;
    }

    res = FontFaceParseMetadata(face);
    if (res != RoboOk) {
        return res;
    }
    res = FontFaceParseClassification(face);
    if (res != RoboOk) {
        return res;
    }
    meta = FontFaceMetadata(face);
    cls = FontFaceClassification(face);
    if (!cls) {
        return RoboParseError;
    }

    family[0] = '\0';
    ps[0] = '\0';
    if (meta) {
        FontMetaField prefField;
        if (FontMetadataPreferredFamily(meta, &prefField) == RoboOk) {
            SnapshotField(family, sizeof(family), &prefField);
        }
        if (!family[0]) {
            SnapshotField(family, sizeof(family), &meta->familyName);
        }
        SnapshotField(ps, sizeof(ps), &meta->postscriptName);
    }

    if ((flags & RoboFontDiscoveryScanAllowContentDup) == 0u &&
        ContentDuplicate(reg, ps, &cls->styleDesc)) {
        if (outDup) {
            *outDup = 1;
        }
        return RoboAlreadyExists;
    }

    res = EnsureRegCap(reg, reg->regCount + 1u);
    if (res != RoboOk) {
        return res;
    }
    slot = &reg->regs[reg->regCount];
    memset(slot, 0, sizeof(*slot));
    slot->id = reg->nextRegId++;
    if (slot->id == 0u) {
        slot->id = reg->nextRegId++;
    }
    slot->sourceId = sourceId;
    slot->faceIndex = faceIndex;
    slot->isCollection = isCollection;
    slot->faceId = GetFontFaceId(face);
    FontFaceRetain(face);
    slot->face = face;
    slot->style = cls->styleDesc;
    CopyStr(slot->family, sizeof(slot->family), family);
    CopyStr(slot->postscriptName, sizeof(slot->postscriptName), ps);
    CopyStr(slot->origin, sizeof(slot->origin), origin ? origin : "");
    reg->regCount++;
    reg->generation++;
    return RoboOk;
}

Result FontRegistryCollectFaces(const FontRegistry *reg,
                                               FontFace ***outFaces,
                                               uint32_t *outCount)
{
    FontFace **arr;
    uint32_t i;
    if (!reg || !outFaces || !outCount) {
        return RoboInvalidArgument;
    }
    *outFaces = NULL;
    *outCount = 0u;
    if (reg->regCount == 0u) {
        return RoboOk;
    }
    arr = (FontFace **)RoboCalloc(reg->regCount, sizeof(*arr));
    if (!arr) {
        return RoboOutOfMemory;
    }
    for (i = 0u; i < reg->regCount; i++) {
        arr[i] = reg->regs[i].face;
    }
    *outFaces = arr;
    *outCount = reg->regCount;
    return RoboOk;
}

Result FontRegistryCollectFacesForFamily(
    const FontRegistry *reg,
    const char *family,
    FontFace ***outFaces,
    uint32_t *outCount)
{
    FontFace **arr;
    uint32_t i, n = 0u;
    if (!reg || !family || !outFaces || !outCount) {
        return RoboInvalidArgument;
    }
    *outFaces = NULL;
    *outCount = 0u;
    for (i = 0u; i < reg->regCount; i++) {
        if (reg->regs[i].family[0] && Utf8CasefoldEq(reg->regs[i].family, family)) {
            n++;
        }
    }
    if (n == 0u) {
        return RoboOk;
    }
    arr = (FontFace **)RoboCalloc(n, sizeof(*arr));
    if (!arr) {
        return RoboOutOfMemory;
    }
    n = 0u;
    for (i = 0u; i < reg->regCount; i++) {
        if (reg->regs[i].family[0] && Utf8CasefoldEq(reg->regs[i].family, family)) {
            arr[n++] = reg->regs[i].face;
        }
    }
    *outFaces = arr;
    *outCount = n;
    return RoboOk;
}

Result FontRegistryRemoveRegistration(FontRegistry *reg,
                                                     FontRegistrationId id)
{
    uint32_t i;
    if (!reg) {
        return RoboInvalidArgument;
    }
    for (i = 0u; i < reg->regCount; i++) {
        if (reg->regs[i].id == id) {
            FreeRegistrationAt(reg, i);
            if (i + 1u < reg->regCount) {
                memmove(&reg->regs[i], &reg->regs[i + 1u],
                        (size_t)(reg->regCount - i - 1u) * sizeof(reg->regs[0]));
            }
            reg->regCount--;
            reg->generation++;
            NotifyInvalidate(reg, 0u, id);
            return RoboOk;
        }
    }
    return RoboNotFound;
}

Result FontRegistryRemoveSourceRegistrations(
    FontRegistry *reg,
    FontDiscoverySourceId sourceId)
{
    uint32_t i = 0u;
    if (!reg) {
        return RoboInvalidArgument;
    }
    while (i < reg->regCount) {
        if (reg->regs[i].sourceId == sourceId) {
            FreeRegistrationAt(reg, i);
            if (i + 1u < reg->regCount) {
                memmove(&reg->regs[i], &reg->regs[i + 1u],
                        (size_t)(reg->regCount - i - 1u) * sizeof(reg->regs[0]));
            }
            reg->regCount--;
        } else {
            i++;
        }
    }
    reg->generation++;
    NotifyInvalidate(reg, sourceId, 0u);
    return RoboOk;
}

static DiscSource *FindSource(FontRegistry *reg,
                                  FontDiscoverySourceId id)
{
    uint32_t i;
    for (i = 0u; i < reg->sourceCount; i++) {
        if (reg->sources[i].active && reg->sources[i].id == id) {
            return &reg->sources[i];
        }
    }
    return NULL;
}

static Result AllocSourceSlot(FontRegistry *reg, DiscSource **out)
{
    uint32_t i;
    if (reg->sourceCount >= RoboFontRegistryMaxSources) {
        /* try reuse inactive */
        for (i = 0u; i < reg->sourceCount; i++) {
            if (!reg->sources[i].active) {
                *out = &reg->sources[i];
                memset(*out, 0, sizeof(**out));
                return RoboOk;
            }
        }
        return RoboOutOfMemory;
    }
    *out = &reg->sources[reg->sourceCount++];
    memset(*out, 0, sizeof(**out));
    return RoboOk;
}

Result FontRegistryAddMemorySource(FontRegistry *reg,
                                                   const char *label,
                                                   const void *bytes,
                                                   size_t size,
                                                   FontDiscoverySourceId *outId)
{
    DiscSource *s;
    Result res;
    if (!reg || !bytes || size == 0u || !outId) {
        return RoboInvalidArgument;
    }
    if (size > RoboFontDiscoveryMaxFileSize) {
        return RoboParseError;
    }
    *outId = 0u;
    res = AllocSourceSlot(reg, &s);
    if (res != RoboOk) {
        return res;
    }
    res = FontSourceCreateMemoryCopy(bytes, size, &s->memSource);
    if (res != RoboOk) {
        return res;
    }
    s->id = reg->nextSrcId++;
    if (s->id == 0u) {
        s->id = reg->nextSrcId++;
    }
    s->kind = RoboFontDiscoverySourceMemory;
    CopyStr(s->path, sizeof(s->path), label ? label : "memory");
    s->active = 1u;
    *outId = s->id;
    return RoboOk;
}

Result FontRegistryAddFileSource(FontRegistry *reg,
                                                 const char *path,
                                                 FontDiscoverySourceId *outId)
{
    DiscSource *s;
    Result res;
    if (!reg || !PathLenOk(path) || !outId) {
        return RoboInvalidArgument;
    }
    *outId = 0u;
    res = AllocSourceSlot(reg, &s);
    if (res != RoboOk) {
        return res;
    }
    s->id = reg->nextSrcId++;
    if (s->id == 0u) {
        s->id = reg->nextSrcId++;
    }
    s->kind = RoboFontDiscoverySourceFile;
    CopyStr(s->path, sizeof(s->path), path);
    s->active = 1u;
    *outId = s->id;
    return RoboOk;
}

Result FontRegistryAddDirectorySource(
    FontRegistry *reg,
    const char *path,
    int recursive,
    FontDiscoverySourceId *outId)
{
    DiscSource *s;
    Result res;
    if (!reg || !PathLenOk(path) || !outId) {
        return RoboInvalidArgument;
    }
    *outId = 0u;
    res = AllocSourceSlot(reg, &s);
    if (res != RoboOk) {
        return res;
    }
    s->id = reg->nextSrcId++;
    if (s->id == 0u) {
        s->id = reg->nextSrcId++;
    }
    s->kind = RoboFontDiscoverySourceDirectory;
    CopyStr(s->path, sizeof(s->path), path);
    s->recursive = recursive ? 1 : 0;
    s->active = 1u;
    *outId = s->id;
    return RoboOk;
}

Result FontRegistryRemoveDiscoverySource(
    FontRegistry *reg,
    FontDiscoverySourceId sourceId)
{
    DiscSource *s;
    if (!reg) {
        return RoboInvalidArgument;
    }
    s = FindSource(reg, sourceId);
    if (!s) {
        return RoboNotFound;
    }
    (void)FontRegistryRemoveSourceRegistrations(reg, sourceId);
    if (s->memSource) {
        FontSourceRelease(s->memSource);
        s->memSource = NULL;
    }
    s->active = 0u;
    return RoboOk;
}

/* ---- Scan ---- */

typedef struct scanCtx {
    FontRegistry *reg;
    DiscSource *src;
    uint32_t flags;
    FontDiscoveryResult *result;
    uint32_t filesBudget;
} ScanCtx;

static void NoteErr(ScanCtx *ctx, Result err)
{
    ctx->result->failed++;
    ctx->result->lastError = err;
}

static Result IngestBytes(ScanCtx *ctx,
                                  const char *diagPath,
                                  const uint8_t *bytes,
                                  size_t size,
                                  FontSource *existingSource)
{
    FontCandidateFormat fmt;
    FontSource *src = existingSource;
    uint32_t faceCount = 0u;
    uint32_t fi;
    uint8_t isColl;
    Result res;
    int ownSrc = 0;

    ctx->result->candidates++;
    if (size < RoboFontDiscoveryMinCandidateBytes) {
        ctx->result->skippedUnsupported++;
        return RoboOk;
    }
    fmt = FontDiscoveryClassifyBytes(bytes, size);
    if (fmt == RoboFontCandidateUnknown || fmt == RoboFontCandidateUnsupported) {
        ctx->result->skippedUnsupported++;
        return RoboOk;
    }
    res = FontSfntProbeFaceCount(bytes, size, &faceCount);
    if (res != RoboOk) {
        ctx->result->skippedUnsupported++;
        if (ctx->flags & RoboFontDiscoveryScanFailFast) {
            return res;
        }
        return RoboOk;
    }
    if (faceCount > RoboFontDiscoveryMaxFacesPerCollection) {
        NoteErr(ctx, RoboParseError);
        if (ctx->flags & RoboFontDiscoveryScanFailFast) {
            return RoboParseError;
        }
        return RoboOk;
    }
    isColl = (fmt == RoboFontCandidateCollection) ? 1u : 0u;
    if (!src) {
        res = FontSourceCreateMemoryCopy(bytes, size, &src);
        if (res != RoboOk) {
            NoteErr(ctx, res);
            return (ctx->flags & RoboFontDiscoveryScanFailFast) ? res : RoboOk;
        }
        ownSrc = 1;
    }

    for (fi = 0u; fi < faceCount; fi++) {
        FontFace *face = NULL;
        int dup = 0;
        res = FontFaceCreate(src, fi, &face);
        if (res != RoboOk) {
            NoteErr(ctx, res);
            if (ctx->flags & RoboFontDiscoveryScanFailFast) {
                if (ownSrc) {
                    FontSourceRelease(src);
                }
                return res;
            }
            continue;
        }
        res = RegisterFace(ctx->reg, ctx->src->id, fi, isColl, face, ctx->flags,
                            diagPath, &dup);
        FontFaceRelease(face);
        if (res == RoboAlreadyExists || dup) {
            ctx->result->skippedDuplicate++;
            continue;
        }
        if (res != RoboOk) {
            NoteErr(ctx, res);
            if (ctx->flags & RoboFontDiscoveryScanFailFast) {
                if (ownSrc) {
                    FontSourceRelease(src);
                }
                return res;
            }
            continue;
        }
        ctx->result->registered++;
    }
    if (ownSrc) {
        FontSourceRelease(src);
    }
    return RoboOk;
}

static Result ScanOneFile(ScanCtx *ctx, const char *path)
{
    uint8_t *bytes = NULL;
    size_t size = 0u;
    Result res;

    if (ctx->filesBudget == 0u) {
        NoteErr(ctx, RoboOutOfMemory);
        return RoboOutOfMemory;
    }
    ctx->filesBudget--;
    ctx->result->filesSeen++;

    if (!FontDiscoveryExtensionIsFont(path)) {
        ctx->result->skippedFilter++;
        return RoboOk;
    }
    if (!ctx->reg->io || !ctx->reg->io->RoboReadFile) {
        return RoboNotSupported;
    }
    res = ctx->reg->io->RoboReadFile(ctx->reg->io->userdata, path,
                                  RoboFontDiscoveryMaxFileSize, &bytes, &size);
    if (res == RoboNotFound || res == RoboPermissionDenied || res == RoboIoError) {
        NoteErr(ctx, res);
        return (ctx->flags & RoboFontDiscoveryScanFailFast) ? res : RoboOk;
    }
    if (res != RoboOk) {
        NoteErr(ctx, res);
        return (ctx->flags & RoboFontDiscoveryScanFailFast) ? res : RoboOk;
    }
    res = IngestBytes(ctx, path, bytes, size, NULL);
    RoboFree(bytes);
    return res;
}

typedef struct dirVisitState {
    ScanCtx *ctx;
    char base[RoboFontDiscoveryMaxPath];
    uint32_t depth;
    Result stopErr;
} DirVisitState;

static int JoinPath(char *out, size_t cap, const char *base, const char *name)
{
    size_t bl = strlen(base);
    size_t nl = strlen(name);
    int needSlash = (bl > 0u && base[bl - 1u] != '/');
    size_t need = bl + (needSlash ? 1u : 0u) + nl + 1u;
    if (need > cap) {
        return 0;
    }
    memcpy(out, base, bl);
    if (needSlash) {
        out[bl++] = '/';
    }
    memcpy(out + bl, name, nl);
    out[bl + nl] = '\0';
    return 1;
}

static Result ScanDirectoryRec(ScanCtx *ctx, const char *path, uint32_t depth);

static int DirVisit(void *ud, const char *name, int IsDirectory)
{
    DirVisitState *st = (DirVisitState *)ud;
    char child[RoboFontDiscoveryMaxPath];
    Result res;

    if (!name || name[0] == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return 0;
    }
    if (!JoinPath(child, sizeof(child), st->base, name)) {
        NoteErr(st->ctx, RoboInvalidArgument);
        return (st->ctx->flags & RoboFontDiscoveryScanFailFast) ? 1 : 0;
    }
    if (IsDirectory) {
        if (!st->ctx->src->recursive) {
            return 0;
        }
        if (st->depth + 1u > RoboFontDiscoveryMaxDirDepth) {
            NoteErr(st->ctx, RoboParseError);
            return (st->ctx->flags & RoboFontDiscoveryScanFailFast) ? 1 : 0;
        }
        res = ScanDirectoryRec(st->ctx, child, st->depth + 1u);
        if (res != RoboOk && (st->ctx->flags & RoboFontDiscoveryScanFailFast)) {
            st->stopErr = res;
            return 1;
        }
        return 0;
    }
    res = ScanOneFile(st->ctx, child);
    if (res != RoboOk && (st->ctx->flags & RoboFontDiscoveryScanFailFast)) {
        st->stopErr = res;
        return 1;
    }
    return 0;
}

static Result ScanDirectoryRec(ScanCtx *ctx, const char *path, uint32_t depth)
{
    DirVisitState st;
    Result res;

    if (!ctx->reg->io || !ctx->reg->io->ListDir) {
        return RoboNotSupported;
    }
    if (ctx->reg->io->IsDirectory && !ctx->reg->io->IsDirectory(ctx->reg->io->userdata, path)) {
        NoteErr(ctx, RoboNotFound);
        return (ctx->flags & RoboFontDiscoveryScanFailFast) ? RoboNotFound : RoboOk;
    }
    memset(&st, 0, sizeof(st));
    st.ctx = ctx;
    CopyStr(st.base, sizeof(st.base), path);
    st.depth = depth;
    st.stopErr = RoboOk;
    res = ctx->reg->io->ListDir(ctx->reg->io->userdata, path, DirVisit, &st);
    if (res != RoboOk) {
        NoteErr(ctx, res);
        return (ctx->flags & RoboFontDiscoveryScanFailFast) ? res : RoboOk;
    }
    if (st.stopErr != RoboOk) {
        return st.stopErr;
    }
    return RoboOk;
}

Result FontRegistryScanSource(FontRegistry *reg,
                                             FontDiscoverySourceId sourceId,
                                             uint32_t flags,
                                             FontDiscoveryResult *outResult)
{
    DiscSource *src;
    ScanCtx ctx;
    FontDiscoveryResult local;
    Result res = RoboOk;

    if (!reg) {
        return RoboInvalidArgument;
    }
    if (!outResult) {
        outResult = &local;
    }
    FontDiscoveryResultInit(outResult);
    src = FindSource(reg, sourceId);
    if (!src) {
        return RoboNotFound;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.reg = reg;
    ctx.src = src;
    ctx.flags = flags;
    ctx.result = outResult;
    ctx.filesBudget = RoboFontDiscoveryMaxFilesPerScan;

    if (src->kind == RoboFontDiscoverySourceMemory) {
        const uint8_t *bytes;
        size_t size;
        if (!src->memSource) {
            return RoboInitError;
        }
        bytes = FontSourceBytes(src->memSource);
        size = FontSourceSize(src->memSource);
        outResult->filesSeen = 1u;
        res = IngestBytes(&ctx, src->path, bytes, size, src->memSource);
    } else if (src->kind == RoboFontDiscoverySourceFile) {
        res = ScanOneFile(&ctx, src->path);
    } else if (src->kind == RoboFontDiscoverySourceDirectory) {
        res = ScanDirectoryRec(&ctx, src->path, 0u);
    } else {
        return RoboInvalidArgument;
    }
    return res;
}

Result FontRegistryScanAll(FontRegistry *reg,
                                          uint32_t flags,
                                          FontDiscoveryResult *outResult)
{
    uint32_t i;
    FontDiscoveryResult acc;
    FontDiscoveryResult one;

    if (!reg) {
        return RoboInvalidArgument;
    }
    FontDiscoveryResultInit(&acc);
    for (i = 0u; i < reg->sourceCount; i++) {
        Result res;
        if (!reg->sources[i].active) {
            continue;
        }
        res = FontRegistryScanSource(reg, reg->sources[i].id, flags, &one);
        acc.filesSeen += one.filesSeen;
        acc.candidates += one.candidates;
        acc.registered += one.registered;
        acc.skippedDuplicate += one.skippedDuplicate;
        acc.skippedUnsupported += one.skippedUnsupported;
        acc.skippedFilter += one.skippedFilter;
        acc.failed += one.failed;
        if (one.lastError != RoboOk) {
            acc.lastError = one.lastError;
        }
        if (res != RoboOk && (flags & RoboFontDiscoveryScanFailFast)) {
            if (outResult) {
                *outResult = acc;
            }
            return res;
        }
    }
    if (outResult) {
        *outResult = acc;
    }
    return RoboOk;
}

Result FontRegistryRefreshSource(FontRegistry *reg,
                                                FontDiscoverySourceId sourceId,
                                                uint32_t flags,
                                                FontDiscoveryResult *outResult)
{
    Result res;
    if (!reg) {
        return RoboInvalidArgument;
    }
    if (!FindSource(reg, sourceId)) {
        return RoboNotFound;
    }
    res = FontRegistryRemoveSourceRegistrations(reg, sourceId);
    if (res != RoboOk) {
        return res;
    }
    return FontRegistryScanSource(reg, sourceId, flags, outResult);
}

Result FontDiscoveryFuzzParse(const uint8_t *data, size_t size)
{
    FontRegistry *reg = NULL;
    FontDiscoveryVirtualIo *vio = NULL;
    FontDiscoverySourceId sid = 0u;
    FontDiscoveryResult result;
    uint8_t tiny[16];
    size_t n;
    size_t i;

    (void)FontDiscoveryClassifyBytes(data, size);
    (void)FontDiscoveryExtensionIsFont("x.ttf");
    (void)FontDiscoveryExtensionIsFont("x.zzz");
    {
        uint32_t fc = 0u;
        (void)FontSfntProbeFaceCount(data, size, &fc);
    }

    if (FontRegistryCreate(&reg) != RoboOk) {
        return RoboOk;
    }
    if (FontDiscoveryVirtualIoCreate(&vio) != RoboOk) {
        FontRegistryDestroy(reg);
        return RoboOk;
    }
    FontRegistrySetIo(reg, FontDiscoveryVirtualIoOps(vio));

    n = size < sizeof(tiny) ? size : sizeof(tiny);
    for (i = 0u; i < n; i++) {
        tiny[i] = data ? data[i] : 0u;
    }
    if (n >= 4u) {
        (void)FontDiscoveryVirtualAddFile(vio, "/Fuzz/a.ttf", tiny, n);
        (void)FontRegistryAddFileSource(reg, "/Fuzz/a.ttf", &sid);
        (void)FontRegistryScanSource(reg, sid, 0u, &result);
    }
    if (data && size > 0u && size <= RoboFontDiscoveryMaxFileSize) {
        (void)FontRegistryAddMemorySource(reg, "fuzz", data, size, &sid);
        (void)FontRegistryScanSource(reg, sid, 0u, &result);
    }
    FontDiscoveryVirtualIoDestroy(vio);
    FontRegistryDestroy(reg);
    return RoboOk;
}
