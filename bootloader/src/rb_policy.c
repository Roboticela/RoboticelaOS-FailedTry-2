#include "roboboot.h"

float rb_splash_visual(float real_progress, uint64_t elapsed_ms, int min_ms)
{
    float cap;
    real_progress = rb_clampf(real_progress, 0.f, 1.f);
    if (min_ms <= 0) return real_progress;
    cap = (float)elapsed_ms / (float)min_ms;
    if (cap > 1.f) cap = 1.f;
    return real_progress < cap ? real_progress : cap;
}

int rb_splash_complete(float real_progress, uint64_t elapsed_ms, int min_ms)
{
    return real_progress >= 0.999f && (int)elapsed_ms >= min_ms;
}

rb_boot_intent rb_policy_intent(int alt_held, int force_menu, const char *next_boot)
{
    if (force_menu || alt_held) return RB_BOOT_PICKER;
    if (next_boot && next_boot[0]) return RB_BOOT_ONCE;
    return RB_BOOT_DEFAULT;
}

int rb_policy_resolve_index(const rb_catalog *c, const rb_env *env, int fallback)
{
    const char *next;
    const char *saved;
    int idx;
    if (!c || c->ntargets == 0) return -1;

    next = rb_env_get(env, "next_boot");
    if (next && next[0]) {
        idx = rb_catalog_find(c, next);
        if (idx >= 0 && c->targets[idx].present && !c->targets[idx].hidden) return idx;
    }
    if (!fallback) {
        idx = rb_catalog_find(c, c->default_id);
        if (idx >= 0 && c->targets[idx].present) return idx;
        saved = rb_env_get(env, "last_boot");
        if (saved && saved[0]) {
            idx = rb_catalog_find(c, saved);
            if (idx >= 0 && c->targets[idx].present) return idx;
        }
    }
    idx = rb_catalog_find(c, c->fallback_id);
    if (idx >= 0 && c->targets[idx].present) return idx;
    for (idx = 0; idx < c->ntargets; idx++) {
        if (c->targets[idx].present && !c->targets[idx].hidden) return idx;
    }
    return 0;
}

int rb_policy_next_index(const rb_catalog *c, int from, int dir)
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
