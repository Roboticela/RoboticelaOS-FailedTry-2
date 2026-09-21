#include "roboboot.h"

float SplashVisual(float realProgress, uint64_t elapsedMs, int minMs)
{
    float cap;
    realProgress = Clampf(realProgress, 0.f, 1.f);
    if (minMs <= 0) return realProgress;
    cap = (float)elapsedMs / (float)minMs;
    if (cap > 1.f) cap = 1.f;
    return realProgress < cap ? realProgress : cap;
}

int SplashComplete(float realProgress, uint64_t elapsedMs, int minMs)
{
    if (realProgress < 0.999f) return 0;
    if (minMs <= 0) return 1;
    return elapsedMs >= (uint64_t)minMs;
}

BootIntent PolicyIntent(int altHeld, int forceMenu, const char *nextBoot)
{
    if (forceMenu || altHeld) return RbBootPicker;
    if (nextBoot && nextBoot[0]) return RbBootOnce;
    return RbBootDefault;
}

int PolicyResolveIndex(const Catalog *c, const Env *env, int fallback)
{
    const char *next;
    const char *saved;
    int idx;
    if (!c || c->ntargets == 0) return -1;

    next = EnvGet(env, "next_boot");
    if (next && next[0]) {
        idx = CatalogFind(c, next);
        if (idx >= 0 && c->targets[idx].present && !c->targets[idx].hidden) return idx;
    }
    if (!fallback) {
        idx = CatalogFind(c, c->defaultId);
        if (idx >= 0 && c->targets[idx].present) return idx;
        saved = EnvGet(env, "last_boot");
        if (saved && saved[0]) {
            idx = CatalogFind(c, saved);
            if (idx >= 0 && c->targets[idx].present) return idx;
        }
    }
    idx = CatalogFind(c, c->fallbackId);
    if (idx >= 0 && c->targets[idx].present) return idx;
    for (idx = 0; idx < c->ntargets; idx++) {
        if (c->targets[idx].present && !c->targets[idx].hidden) return idx;
    }
    return 0;
}

int PolicyNextIndex(const Catalog *c, int from, int dir)
{
    int i, n, idx;
    if (!c || c->ntargets <= 0) return 0;
    n = c->ntargets;
    idx = from;
    for (i = 0; i < n; i++) {
        idx = (idx + dir) % n;
        if (idx < 0) idx += n;
        if (c->targets[idx].present && !c->targets[idx].hidden) return idx;
    }
    return from;
}
