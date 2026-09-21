#include "roboboot.h"

#include <stdio.h>
#include <string.h>

RoboRbStatus Elf64Info(const uint8_t *data, size_t len, uint64_t *entry, uint64_t *phoff, int *phnum)
{
    uint16_t type, machine, ehsize, phentsize, phn;
    uint32_t version;
    uint64_t eEntry, ePhoff;
    if (!data || len < 64) return RbErrFormat;
    if (data[0] != 0x7f || data[1] != 'E' || data[2] != 'L' || data[3] != 'F') return RbErrFormat;
    if (data[4] != 2 || data[5] != 1) return RbErrFormat; /* ELF64 LE */
    memcpy(&type, data + 16, 2);
    memcpy(&machine, data + 18, 2);
    memcpy(&version, data + 20, 4);
    memcpy(&eEntry, data + 24, 8);
    memcpy(&ePhoff, data + 32, 8);
    memcpy(&ehsize, data + 52, 2);
    memcpy(&phentsize, data + 54, 2);
    memcpy(&phn, data + 56, 2);
    if (type != 2 && type != 3) return RbErrFormat; /* EXEC or DYN */
    if (machine != 62) return RbErrFormat;          /* x86_64 */
    if (version != 1 || ehsize != 64 || phentsize != 56 || phn == 0) return RbErrFormat;
    if (ePhoff + (uint64_t)phn * 56ull > len) return RbErrFormat;
    if (entry) *entry = eEntry;
    if (phoff) *phoff = ePhoff;
    if (phnum) *phnum = (int)phn;
    return RbOk;
}

int Elf64Phdrs(const uint8_t *data, size_t len, Phdr *out, int cap)
{
    uint64_t phoff = 0;
    int phnum = 0, i, n = 0;
    if (Elf64Info(data, len, 0, &phoff, &phnum) != RbOk) return 0;
    if (!out || cap < 1) return 0;
    for (i = 0; i < phnum && n < cap; i++) {
        const uint8_t *p = data + phoff + (uint64_t)i * 56ull;
        uint32_t type, flags;
        memcpy(&type, p, 4);
        memcpy(&flags, p + 4, 4);
        if (type != 1) continue; /* PT_LOAD */
        memcpy(&out[n].offset, p + 8, 8);
        memcpy(&out[n].vaddr, p + 16, 8);
        memcpy(&out[n].paddr, p + 24, 8);
        memcpy(&out[n].filesz, p + 32, 8);
        memcpy(&out[n].memsz, p + 40, 8);
        memcpy(&out[n].align, p + 48, 8);
        out[n].type = type;
        out[n].flags = flags;
        n++;
    }
    return n;
}

static void ResolvePayload(const char *root, const Target *t, const char *rel, char *out, size_t cap)
{
    if (!rel || !rel[0]) {
        out[0] = 0;
        return;
    }
    if (rel[0] && rel[1] == ':') {
        Strcpy(out, cap, rel);
        return;
    }
    if (t && t->volume[0]) {
        char base[RbMaxPath];
        Join(base, sizeof(base), root, "volumes");
        Join(base, sizeof(base), base, t->volume);
        Join(out, cap, base, rel[0] == '/' ? rel + 1 : rel);
        return;
    }
    Join(out, cap, root, rel[0] == '/' ? rel + 1 : rel);
}

static RoboRbStatus LoadBlob(const char *root, const Target *t, const char *rel, ImageBlob *blob)
{
    char path[RbMaxPath];
    void *data = NULL;
    size_t len = 0;
    RoboRbStatus st;
    Zero(blob, sizeof(*blob));
    if (!rel || !rel[0]) return RbOk;
    ResolvePayload(root, t, rel, path, sizeof(path));
    Bootlogf(NULL, "Blob  open  %s", path);
    if (!Plat || !Plat->RoboReadFile) return RbErrIo;
    st = Plat->RoboReadFile(path, &data, &len);
    if (st != RbOk) {
        Bootlogf(NULL, "Blob  failed  %s  (%d)", path, st);
        return st;
    }
    blob->bytes = (uint8_t *)data;
    blob->size = len;
    blob->valid = 1;
    Bootlogf(NULL, "Blob  %zu bytes  %s", len, path);
    return RbOk;
}

void HandoffFree(Handoff *h)
{
    int i;
    if (!h) return;
    RoboRbFree(h->kernel.bytes);
    RoboRbFree(h->ramdisk.bytes);
    for (i = 0; i < h->nextras; i++) RoboRbFree(h->extras[i].bytes);
    Zero(h, sizeof(*h));
}

RoboRbStatus LoaderPrepare(const Target *t, const char *root, Handoff *h)
{
    RoboRbStatus st;
    int i;
    if (!t || !h) return RbErr;
    HandoffFree(h);
    h->protocol = t->kind;
    Strcpy(h->cmdline, sizeof(h->cmdline), t->args);
    Bootlogf(NULL, "Prepare  %s  %s", t->id, KindName(t->kind));

    if (t->kind == RbKindFirmware || t->kind == RbKindReboot || t->kind == RbKindHalt) {
        Bootlog(NULL, "Prepare  no payload");
        return RbOk;
    }

    if (t->kind == RbKindEfiChain) {
        Bootlog(NULL, "Prepare  EFI chain");
        st = LoadBlob(root, t, t->chain[0] ? t->chain : t->kernel, &h->kernel);
        return st;
    }

    Bootlog(NULL, "Prepare  kernel ELF");
    st = LoadBlob(root, t, t->kernel, &h->kernel);
    if (st != RbOk) return st;

    if (t->kind == RbKindKernel || t->kind == RbKindMultiboot2) {
        uint64_t entry = 0, phoff = 0;
        int phnum = 0, nph, pi;
        Phdr ph[RbMaxPhdrs];
        st = Elf64Info(h->kernel.bytes, h->kernel.size, &entry, &phoff, &phnum);
        if (st != RbOk) {
            Bootlogf(NULL, "ELF  parse failed  (%d)", st);
            return st;
        }
        h->kernel.entry = entry;
        h->kernel.phys = 0x100000;
        Bootlogf(NULL, "ELF  entry 0x%llx  phoff 0x%llx  phnum %d",
                    (unsigned long long)entry, (unsigned long long)phoff, phnum);
        nph = Elf64Phdrs(h->kernel.bytes, h->kernel.size, ph, RbMaxPhdrs);
        Bootlogf(NULL, "ELF  %d PT_LOAD", nph);
        for (pi = 0; pi < nph; pi++) {
            Bootlogf(NULL, "  PHDR  va 0x%llx  file %llu  mem %llu  %c%c%c",
                        (unsigned long long)ph[pi].vaddr,
                        (unsigned long long)ph[pi].filesz,
                        (unsigned long long)ph[pi].memsz,
                        (ph[pi].flags & 4) ? 'R' : '-',
                        (ph[pi].flags & 2) ? 'W' : '-',
                        (ph[pi].flags & 1) ? 'X' : '-');
        }
    }

    if (t->ramdisk[0]) {
        Bootlog(NULL, "Prepare  InitFS");
        st = LoadBlob(root, t, t->ramdisk, &h->ramdisk);
        if (st != RbOk) return st;
    } else
        Bootlog(NULL, "Prepare  no InitFS");
    for (i = 0; i < t->nmodules && h->nextras < RbMaxModules; i++) {
        Bootlogf(NULL, "Prepare  module %d  %s", i, t->modules[i]);
        st = LoadBlob(root, t, t->modules[i], &h->extras[h->nextras]);
        if (st != RbOk) return st;
        h->nextras++;
    }
    Bootlog(NULL, "Prepare  ready");
    return RbOk;
}
