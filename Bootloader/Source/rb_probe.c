#include "roboboot.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    Probe *out;
    char root[RbMaxPath];
} probeCtx;

static int FileExists(const char *path)
{
    void *data = NULL;
    size_t len = 0;
    if (!Plat || !Plat->RoboReadFile) return 0;
    if (Plat->RoboReadFile(path, &data, &len) != RbOk) return 0;
    RoboRbFree(data);
    return 1;
}

static void OnVol(const char *name, int isDir, void *user)
{
    probeCtx *ctx = (probeCtx *)user;
    Volume *v;
    char path[RbMaxPath];
    char probe[RbMaxPath];
    if (!isDir || !name || name[0] == '.') return;
    if (ctx->out->nvols >= 16) return;
    v = &ctx->out->vols[ctx->out->nvols];
    Zero(v, sizeof(*v));
    Strcpy(v->name, sizeof(v->name), name);
    Join(path, sizeof(path), ctx->root, name);
    Strcpy(v->path, sizeof(v->path), path);
    Strcpy(v->fs, sizeof(v->fs), "hostfs");
    Join(probe, sizeof(probe), path, "kernel.elf");
    if (FileExists(probe)) {
        v->hasKernel = 1;
        Bootlogf(NULL, "Probe  %s/kernel.elf", name);
    }
    Join(probe, sizeof(probe), path, "sys/kernel.elf");
    if (FileExists(probe)) {
        v->hasKernel = 1;
        Bootlogf(NULL, "Probe  %s/sys/kernel.elf", name);
    }
    Join(probe, sizeof(probe), path, "EFI/Microsoft/Boot/bootmgfw.efi");
    if (FileExists(probe)) {
        v->hasEfi = 1;
        Bootlogf(NULL, "Probe  %s  Windows bootmgfw", name);
    }
    Bootlogf(NULL, "Probe  dir  %s", name);
    ctx->out->nvols++;
}

RoboRbStatus ProbeVolumes(const char *volumesRoot, Probe *out)
{
    probeCtx ctx;
    if (!out) return RbErr;
    Zero(out, sizeof(*out));
    if (!volumesRoot || !Plat || !Plat->ListDir) return RbOk;
    Zero(&ctx, sizeof(ctx));
    ctx.out = out;
    Strcpy(ctx.root, sizeof(ctx.root), volumesRoot);
    return Plat->ListDir(volumesRoot, OnVol, &ctx);
}

void ProbeApply(Catalog *c, const Probe *p)
{
    int i, j;
    if (!c || !p) return;
    for (i = 0; i < c->ntargets; i++) {
        Target *t = &c->targets[i];
        if (!t->volume[0]) {
            t->present = 1;
            continue;
        }
        t->present = 0;
        for (j = 0; j < p->nvols; j++) {
            if (StrEq(p->vols[j].name, t->volume)) {
                t->present = 1;
                break;
            }
        }
        /* firmware / reboot / halt do not need a volume */
        if (t->kind == RbKindFirmware || t->kind == RbKindReboot || t->kind == RbKindHalt)
            t->present = 1;
    }
}
