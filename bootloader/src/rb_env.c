#include "roboboot.h"

#include <stdio.h>
#include <string.h>

rb_status rb_env_parse(const char *text, rb_env *out)
{
    const char *p;
    if (!out) return RB_ERR;
    rb_zero(out, sizeof(*out));
    if (!text) return RB_OK;
    if (rb_strncmp(text, "ROBOENV1", 8) == 0) {
        text += 8;
        if (*text == '\n' || *text == '\r') {
            if (*text == '\r' && text[1] == '\n') text += 2;
            else text++;
        }
    }
    p = text;
    while (*p) {
        char line[256];
        int n = 0;
        char *eq;
        while (p[n] && p[n] != '\n') n++;
        if (n > 255) n = 255;
        memcpy(line, p, (size_t)n);
        line[n] = 0;
        p += n;
        if (*p == '\n') p++;
        if (line[0] == 0 || line[0] == '#') continue;
        eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        rb_env_set(out, line, eq + 1);
    }
    return RB_OK;
}

void rb_env_set(rb_env *env, const char *key, const char *val)
{
    int i;
    if (!env || !key) return;
    for (i = 0; i < env->n; i++) {
        if (rb_str_eq(env->keys[i], key)) {
            rb_strcpy(env->vals[i], sizeof(env->vals[i]), val ? val : "");
            return;
        }
    }
    if (env->n >= RB_MAX_ENV) return;
    rb_strcpy(env->keys[env->n], sizeof(env->keys[0]), key);
    rb_strcpy(env->vals[env->n], sizeof(env->vals[0]), val ? val : "");
    env->n++;
}

const char *rb_env_get(const rb_env *env, const char *key)
{
    int i;
    if (!env || !key) return NULL;
    for (i = 0; i < env->n; i++) {
        if (rb_str_eq(env->keys[i], key)) return env->vals[i];
    }
    return NULL;
}

int rb_debug_enabled(const rb_env *env)
{
    const char *v = rb_env_get(env, "debug");
    if (!v || !v[0]) return 0;
    if (v[0] == '0' && v[1] == 0) return 0;
    if (rb_str_eq(v, "off") || rb_str_eq(v, "false") || rb_str_eq(v, "no")) return 0;
    return 1;
}

void rb_debug_set(rb_env *env, int on)
{
    rb_env_set(env, "debug", on ? "1" : "0");
}

rb_status rb_env_load(const char *path, rb_env *out)
{
    void *data = NULL;
    size_t len = 0;
    rb_status st;
    char *text;
    if (!out) return RB_ERR;
    rb_zero(out, sizeof(*out));
    rb_strcpy(out->path, sizeof(out->path), path ? path : "");
    if (!path || !path[0] || !rb_plat || !rb_plat->read_file) return RB_OK;
    st = rb_plat->read_file(path, &data, &len);
    if (st != RB_OK) return RB_OK; /* missing env is fine */
    text = (char *)rb_alloc(len + 1);
    if (!text) {
        rb_free(data);
        return RB_ERR_NOMEM;
    }
    memcpy(text, data, len);
    text[len] = 0;
    rb_free(data);
    st = rb_env_parse(text, out);
    rb_strcpy(out->path, sizeof(out->path), path);
    rb_free(text);
    return st;
}

rb_status rb_env_save(const rb_env *env)
{
    char block[RB_ENV_BLOCK];
    int i, n = 0;
    if (!env || !env->path[0] || !rb_plat || !rb_plat->write_file) return RB_ERR_IO;
    memset(block, '#', sizeof(block));
    n = snprintf(block, sizeof(block), "ROBOENV1\n");
    for (i = 0; i < env->n; i++) {
        n += snprintf(block + n, sizeof(block) - (size_t)n, "%s=%s\n", env->keys[i], env->vals[i]);
        if (n >= (int)sizeof(block) - 2) break;
    }
    if (n < (int)sizeof(block)) block[n] = '#';
    block[RB_ENV_BLOCK - 1] = 0;
    return rb_plat->write_file(env->path, block, RB_ENV_BLOCK);
}
