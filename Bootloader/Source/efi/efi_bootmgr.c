#include "efi.h"
#include "roboboot.h"

#include <string.h>

static EFI_GUID kGlobalVar = {0x8BE4DF61, 0x93CA, 0x11d2, {0xAA, 0x0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C}};
static EFI_GUID kLiGuid = {0x5b1b31a1, 0x9562, 0x11d2, {0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static EFI_GUID kLiDpGuid = {0xef9fc172, 0xa1b2, 0x4693, {0xb3, 0x27, 0x6d, 0x32, 0xfc, 0x41, 0x60, 0x03}};
static EFI_GUID kDpGuid = {0x09576e91, 0x6d3f, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static EFI_GUID kFsGuid = {0x964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};

static const CHAR16 kDesc[] = {
    'R','o','b','o','t','i','c','e','l','a','O','S', 0
};
static const CHAR16 kFile[] = {
    '\\','E','F','I','\\','R','o','b','o','t','i','c','e','l','a','\\','B','O','O','T','X','6','4','.','E','F','I', 0
};
static const CHAR16 kBootorder[] = {
    'B','o','o','t','O','r','d','e','r', 0
};

#define DP_LEN(n) ((UINTN)((n)->Length[0] | ((UINTN)(n)->Length[1] << 8)))
#define NV_ATTR (EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS)

static void DpSetLen(EFI_DEVICE_PATH_PROTOCOL *n, UINTN len)
{
    n->Length[0] = (uint8_t)(len & 0xFF);
    n->Length[1] = (uint8_t)((len >> 8) & 0xFF);
}

static UINTN DpSize(EFI_DEVICE_PATH_PROTOCOL *dp)
{
    UINTN n = 0;
    if (!dp) return 0;
    for (;;) {
        UINTN l = DP_LEN(dp);
        if (l < 4) break;
        n += l;
        if (dp->Type == END_DEVICE_PATH_TYPE && dp->SubType == END_ENTIRE_DEVICE_PATH_SUBTYPE)
            break;
        dp = (EFI_DEVICE_PATH_PROTOCOL *)((uint8_t *)dp + l);
    }
    return n;
}

static UINTN U16Len(const CHAR16 *s)
{
    UINTN n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

static int U16Eq(const CHAR16 *a, const CHAR16 *b)
{
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static void BootVarName(CHAR16 *out, unsigned idx)
{
    static const CHAR16 hex[16] = {
        '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'
    };
    out[0] = 'B';
    out[1] = 'o';
    out[2] = 'o';
    out[3] = 't';
    out[4] = hex[(idx >> 12) & 0xF];
    out[5] = hex[(idx >> 8) & 0xF];
    out[6] = hex[(idx >> 4) & 0xF];
    out[7] = hex[idx & 0xF];
    out[8] = 0;
}

static void LogSt(const char *msg, EFI_STATUS st)
{
    char buf[80];
    unsigned i;
    unsigned long long v = (unsigned long long)st;
    const char *hex = "0123456789ABCDEF";
    for (i = 0; i < sizeof(buf); i++) buf[i] = 0;
    i = 0;
    while (msg[i] && i < 48) {
        buf[i] = msg[i];
        i++;
    }
    buf[i++] = ' ';
    buf[i++] = '0';
    buf[i++] = 'x';
    {
        int n;
        char tmp[16];
        int t = 0;
        if (v == 0) tmp[t++] = '0';
        while (v && t < 16) {
            tmp[t++] = hex[v & 0xF];
            v >>= 4;
        }
        for (n = t - 1; n >= 0 && i < 78; n--)
            buf[i++] = tmp[n];
    }
    buf[i] = 0;
        EfiLog(buf);
        Bootlog(NULL, buf);
}

static EFI_STATUS GetVar(CHAR16 *name, void *data, UINTN *sz)
{
    uint32_t attr = 0;
    return gRT->GetVariable(name, &kGlobalVar, &attr, sz, data);
}

static UINTN DpPrefixWithoutFiles(EFI_DEVICE_PATH_PROTOCOL *dp)
{
    UINTN keep = 0;
    UINTN total = 0;
    if (!dp) return 0;
    for (;;) {
        UINTN l = DP_LEN(dp);
        if (l < 4) break;
        if (dp->Type == END_DEVICE_PATH_TYPE) break;
        total += l;
        if (!(dp->Type == MEDIA_DEVICE_PATH && dp->SubType == MEDIA_FILEPATH_DP))
            keep = total;
        dp = (EFI_DEVICE_PATH_PROTOCOL *)((uint8_t *)dp + l);
    }
    return keep;
}

static int proto(EFI_HANDLE h, EFI_GUID *guid, void **out)
{
    EFI_STATUS st;
    *out = 0;
    if (!h) return 0;
    st = gBS->HandleProtocol(h, guid, out);
    if (st == EFI_SUCCESS && *out) return 1;
    if (gBS->OpenProtocol) {
        st = gBS->OpenProtocol(h, guid, out, gImage, 0, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
        if (st == EFI_SUCCESS && *out) return 1;
    }
    return 0;
}

static EFI_DEVICE_PATH_PROTOCOL *DpFromFs(EFI_HANDLE prefer)
{
    UINTN count = 0;
    EFI_HANDLE *handles = 0;
    EFI_DEVICE_PATH_PROTOCOL *found = 0;
    UINTN i;
    if (!gBS->LocateHandleBuffer) return 0;
    if (gBS->LocateHandleBuffer(EFI_LOCATE_BY_PROTOCOL, &kFsGuid, 0, &count, &handles) != EFI_SUCCESS)
        return 0;
    for (i = 0; i < count; i++) {
        EFI_DEVICE_PATH_PROTOCOL *dp = 0;
        if (!proto(handles[i], &kDpGuid, (void **)&dp)) continue;
        if (prefer && handles[i] == prefer) {
            found = dp;
            break;
        }
        if (!found) found = dp;
    }
    if (handles) gBS->FreePool(handles);
    return found;
}

static EFI_DEVICE_PATH_PROTOCOL *GetParentDevpath(EFI_LOADED_IMAGE_PROTOCOL *li)
{
    EFI_DEVICE_PATH_PROTOCOL *dp = 0;
    if (proto(gImage, &kLiDpGuid, (void **)&dp)) return dp;
    if (proto(gImage, &kDpGuid, (void **)&dp)) return dp;
    if (li && proto(li->DeviceHandle, &kDpGuid, (void **)&dp)) return dp;
    return DpFromFs(li ? li->DeviceHandle : 0);
}

static EFI_DEVICE_PATH_PROTOCOL *MakeFilePath(EFI_DEVICE_PATH_PROTOCOL *parent)
{
    UINTN copySz, fileChars, nodeSz, total;
    EFI_DEVICE_PATH_PROTOCOL *out, *node, *end;
    uint8_t *p;

    copySz = DpPrefixWithoutFiles(parent);
    if (!copySz) return 0;
    fileChars = U16Len(kFile) + 1;
    nodeSz = 4 + fileChars * sizeof(CHAR16);
    total = copySz + nodeSz + 4;
    if (gBS->AllocatePool(EfiBootServicesData, total, (void **)&out) != EFI_SUCCESS || !out)
        return 0;
    memset(out, 0, total);
    memcpy(out, parent, copySz);
    p = (uint8_t *)out + copySz;
    node = (EFI_DEVICE_PATH_PROTOCOL *)p;
    node->Type = MEDIA_DEVICE_PATH;
    node->SubType = MEDIA_FILEPATH_DP;
    DpSetLen(node, nodeSz);
    memcpy(p + 4, kFile, fileChars * sizeof(CHAR16));
    end = (EFI_DEVICE_PATH_PROTOCOL *)(p + nodeSz);
    end->Type = END_DEVICE_PATH_TYPE;
    end->SubType = END_ENTIRE_DEVICE_PATH_SUBTYPE;
    DpSetLen(end, 4);
    return out;
}

static uint8_t *MakeLoadOption(EFI_DEVICE_PATH_PROTOCOL *path, UINTN *outSz)
{
    UINTN descBytes, pathSz, total;
    uint8_t *buf;
    uint32_t attr = LOAD_OPTION_ACTIVE;
    uint16_t flen;

    pathSz = DpSize(path);
    descBytes = (U16Len(kDesc) + 1) * sizeof(CHAR16);
    total = 4 + 2 + descBytes + pathSz;
    if (gBS->AllocatePool(EfiBootServicesData, total, (void **)&buf) != EFI_SUCCESS || !buf)
        return 0;
    flen = (uint16_t)pathSz;
    memcpy(buf, &attr, 4);
    memcpy(buf + 4, &flen, 2);
    memcpy(buf + 6, kDesc, descBytes);
    memcpy(buf + 6 + descBytes, path, pathSz);
    *outSz = total;
    return buf;
}

static int DescIsOurs(const uint8_t *opt, UINTN n)
{
    UINTN descOff = 6;
    if (n < descOff + sizeof(kDesc)) return 0;
    return U16Eq((const CHAR16 *)(opt + descOff), kDesc);
}

static int FindOurs(unsigned *found)
{
    unsigned idx;
    uint8_t tmp[1024];
    for (idx = 0; idx < 0x100; idx++) {
        CHAR16 name[9];
        UINTN sz = sizeof(tmp);
        BootVarName(name, idx);
        if (GetVar(name, tmp, &sz) != EFI_SUCCESS) continue;
        if (DescIsOurs(tmp, sz)) {
            *found = idx;
            return 1;
        }
    }
    return 0;
}

static int SlotIsFree(unsigned idx)
{
    CHAR16 name[9];
    uint8_t scratch[8];
    UINTN sz = sizeof(scratch);
    EFI_STATUS st;
    BootVarName(name, idx);
    st = GetVar(name, scratch, &sz);
    return st == EFI_NOT_FOUND;
}

static int PickSlot(const uint16_t *order, UINTN norder, unsigned *slot)
{
    unsigned idx;
    UINTN i;
    for (idx = 0; idx < 0x100; idx++) {
        int used = 0;
        for (i = 0; i < norder; i++) {
            if (order[i] == (uint16_t)idx) { used = 1; break; }
        }
        if (used) continue;
        if (!SlotIsFree(idx)) continue;
        *slot = idx;
        return 1;
    }
    return 0;
}

static uint16_t *ReadBootOrder(UINTN *norder)
{
    uint8_t probe[16];
    UINTN sz = sizeof(probe);
    EFI_STATUS st;
    uint16_t *order = 0;

    *norder = 0;
    st = GetVar((CHAR16 *)kBootorder, probe, &sz);
    if (st == EFI_NOT_FOUND) return 0;
    if (st == EFI_SUCCESS) {
        if (sz < sizeof(uint16_t)) return 0;
        if (gBS->AllocatePool(EfiBootServicesData, sz, (void **)&order) != EFI_SUCCESS || !order)
            return 0;
        memcpy(order, probe, sz);
        *norder = sz / sizeof(uint16_t);
        return order;
    }
    if (st != EFI_BUFFER_TOO_SMALL || sz < sizeof(uint16_t)) return 0;
    if (gBS->AllocatePool(EfiBootServicesData, sz, (void **)&order) != EFI_SUCCESS || !order)
        return 0;
    if (GetVar((CHAR16 *)kBootorder, order, &sz) != EFI_SUCCESS) {
        gBS->FreePool(order);
        return 0;
    }
    *norder = sz / sizeof(uint16_t);
    return order;
}

static EFI_STATUS SetBootOrder(unsigned ours, const uint16_t *old, UINTN nold)
{
    uint16_t order[64];
    UINTN n = 0;
    UINTN i;
    order[n++] = (uint16_t)ours;
    for (i = 0; i < nold && n < 64; i++) {
        if (old[i] == (uint16_t)ours) continue;
        order[n++] = old[i];
    }
    return gRT->SetVariable((CHAR16 *)kBootorder, &kGlobalVar, NV_ATTR, n * sizeof(uint16_t), order);
}

void EfiInstallBootEntry(void)
{
    EFI_LOADED_IMAGE_PROTOCOL *li = 0;
    EFI_DEVICE_PATH_PROTOCOL *devpath = 0;
    EFI_DEVICE_PATH_PROTOCOL *full = 0;
    uint8_t *opt = 0;
    UINTN optSz = 0;
    uint16_t *bootOrder = 0;
    UINTN norder = 0;
    unsigned slot = 0;
    int existed = 0;
    CHAR16 varname[9];
    EFI_STATUS st;

    if (!gRT || !gBS || !gImage) return;
    if (!gRT->GetVariable || !gRT->SetVariable) return;
    st = gBS->HandleProtocol(gImage, &kLiGuid, (void **)&li);
    if (st != EFI_SUCCESS || !li) {
        LogSt("RoboBoot firmware boot entry LoadedImage", st);
        return;
    }
    devpath = GetParentDevpath(li);
    if (!devpath) {
        Bootlog(NULL, "BootOrder  DevicePath missing");
        return;
    }

    full = MakeFilePath(devpath);
    if (!full) {
        Bootlog(NULL, "BootOrder  path alloc failed");
        return;
    }
    opt = MakeLoadOption(full, &optSz);
    gBS->FreePool(full);
    if (!opt) {
        Bootlog(NULL, "BootOrder  option alloc failed");
        return;
    }

    bootOrder = ReadBootOrder(&norder);

    if (FindOurs(&slot))
        existed = 1;
    else if (!PickSlot(bootOrder, norder, &slot)) {
        Bootlog(NULL, "BootOrder  no Boot#### slot");
        gBS->FreePool(opt);
        if (bootOrder) gBS->FreePool(bootOrder);
        return;
    }

    BootVarName(varname, slot);
    st = gRT->SetVariable(varname, &kGlobalVar, NV_ATTR, optSz, opt);
    gBS->FreePool(opt);
    if (st != EFI_SUCCESS) {
        LogSt("RoboBoot firmware boot entry not saved", st);
        if (bootOrder) gBS->FreePool(bootOrder);
        return;
    }
    SetBootOrder(slot, bootOrder, norder);
    if (bootOrder) gBS->FreePool(bootOrder);
    if (existed)
        Bootlog(NULL, "BootOrder  RoboticelaOS already current");
    else
        Bootlogf(NULL, "BootOrder  installed  Boot%04X", slot);
}
