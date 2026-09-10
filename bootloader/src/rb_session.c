#include "roboboot.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static rb_status load_path_image(rb_session *ss, const char *rel, rb_image *im)
{
    char path[RB_MAX_PATH];
    void *data = NULL;
    size_t len = 0;
    rb_status st;
    rb_image_free(im);
    rb_join(path, sizeof(path), ss->plat->root, rel);
    st = ss->plat->read_file(path, &data, &len);
    if (st != RB_OK) return st;
    st = rb_image_load_mem(data, len, im);
    rb_free(data);
    return st;
}

static rb_status load_path_font(rb_session *ss, const char *rel, rb_font *font)
{
    char path[RB_MAX_PATH];
    void *data = NULL;
    size_t len = 0;
    rb_status st;
    rb_font_free(font);
    rb_join(path, sizeof(path), ss->plat->root, rel);
    st = ss->plat->read_file(path, &data, &len);
    if (st != RB_OK) return st;
    st = rb_font_load_mem(data, len, font);
    rb_free(data);
    return st;
}

static float bump_load(rb_session *ss, float to)
{
    if (to > ss->load_real) ss->load_real = to;
    return ss->load_real;
}

static rb_status load_resources(rb_session *ss)
{
    char path[RB_MAX_PATH];
    int i;
    rb_status st;

    bump_load(ss, 0.06f);
    rb_join(path, sizeof(path), ss->plat->root, "config/roboboot.manifest");
    st = rb_manifest_load_path(path, &ss->catalog);
    if (st != RB_OK) {
        rb_logf("manifest load failed (%d)", st);
        return st;
    }
    bump_load(ss, 0.18f);

    rb_join(path, sizeof(path), ss->plat->root, ss->catalog.env_path);
    rb_env_load(path, &ss->env);
    bump_load(ss, 0.28f);

    rb_join(path, sizeof(path), ss->plat->root, "volumes");
    rb_probe_volumes(path, &ss->probe);
    rb_probe_apply(&ss->catalog, &ss->probe);
    bump_load(ss, 0.48f);

    load_path_image(ss, ss->catalog.splash_logo, &ss->logo);
    bump_load(ss, 0.62f);

    for (i = 0; i < ss->catalog.ntargets; i++) {
        if (ss->catalog.targets[i].icon[0])
            load_path_image(ss, ss->catalog.targets[i].icon, &ss->icons[i]);
        bump_load(ss, 0.62f + 0.22f * ((float)(i + 1) / (float)ss->catalog.ntargets));
    }

    load_path_font(ss, "assets/fonts/ui-title.rbfn", &ss->font_title);
    load_path_font(ss, "assets/fonts/ui-body.rbfn", &ss->font_body);
    load_path_font(ss, "assets/fonts/ui-caption.rbfn", &ss->font_caption);
    bump_load(ss, 0.96f);

    ss->selected = rb_policy_resolve_index(&ss->catalog, &ss->env, 0);
    if (ss->selected < 0) ss->selected = 0;
    ss->timeout_left_ms = ss->catalog.picker_timeout_s * 1000;
    bump_load(ss, 1.f);
    return RB_OK;
}

rb_status rb_session_load(rb_session *ss)
{
    return load_resources(ss);
}

void rb_session_init(rb_session *ss, const rb_platform *plat)
{
    rb_zero(ss, sizeof(*ss));
    ss->plat = plat;
    rb_plat = plat;
    ss->phase = RB_PHASE_LOAD;
    ss->running = 1;
    ss->selected = 0;
    ss->boot_index = -1;
    ss->fade = 0.f;
    ss->sel_anim = 1.f;
    ss->cursor_on = 0;
}

void rb_session_free(rb_session *ss)
{
    int i;
    if (!ss) return;
    rb_image_free(&ss->logo);
    for (i = 0; i < RB_MAX_TARGETS; i++) rb_image_free(&ss->icons[i]);
    rb_font_free(&ss->font_title);
    rb_font_free(&ss->font_body);
    rb_font_free(&ss->font_caption);
    rb_handoff_free(&ss->handoff);
    rb_free(ss->fb.px);
    rb_zero(ss, sizeof(*ss));
}

static int key_hit(const rb_input *in, int key)
{
    int i;
    for (i = 0; i < in->nkeys; i++)
        if (in->keys[i] == key) return 1;
    return 0;
}

static int picker_chord(const rb_session *ss)
{
    int i;
    uint32_t need = ss->catalog.picker_key ? ss->catalog.picker_key : RB_MOD_ALT;
    if (!(ss->input.mods & need)) return 0;
    for (i = 0; i < ss->input.nchars; i++) {
        int ch = ss->input.chars[i];
        if (ch == 'm' || ch == 'M') return 1;
    }
    return 0;
}

static int consume_hotkey(rb_session *ss)
{
    int i, k;
    for (k = 0; k < ss->input.nchars; k++) {
        int ch = ss->input.chars[k];
        if (ch >= 'A' && ch <= 'Z') ch += 32;
        for (i = 0; i < ss->catalog.ntargets; i++) {
            int hk = ss->catalog.targets[i].hotkey;
            if (hk >= 'A' && hk <= 'Z') hk += 32;
            if (hk && hk == ch && ss->catalog.targets[i].present) {
                ss->selected = i;
                ss->sel_anim = 0.f;
                return 1;
            }
        }
    }
    return 0;
}

static rb_status try_boot(rb_session *ss, int idx)
{
    rb_target *t;
    rb_status st;
    if (idx < 0 || idx >= ss->catalog.ntargets) return RB_ERR_NOTFOUND;
    t = &ss->catalog.targets[idx];
    if (t->restricted && t->lock[0]) {
        ss->phase = RB_PHASE_AUTH;
        ss->auth_len = 0;
        ss->auth_buf[0] = 0;
        ss->auth_fail = 0;
        return RB_OK;
    }
    st = rb_loader_prepare(t, ss->plat->root, &ss->handoff);
    if (st != RB_OK) {
        rb_logf("prepare failed for %s (%d)", t->id, st);
        idx = rb_policy_resolve_index(&ss->catalog, &ss->env, 1);
        if (idx < 0 || idx == ss->boot_index) return st;
        ss->boot_index = idx;
        ss->selected = idx;
        return try_boot(ss, idx);
    }
    if (ss->catalog.save_last) {
        rb_env_set(&ss->env, "last_boot", t->id);
        rb_env_set(&ss->env, "next_boot", "");
        rb_env_save(&ss->env);
    }
    if (t->kind == RB_KIND_FIRMWARE)
        st = ss->plat->firmware_setup ? ss->plat->firmware_setup() : RB_OK;
    else if (t->kind == RB_KIND_REBOOT)
        st = ss->plat->reboot ? ss->plat->reboot() : RB_OK;
    else if (t->kind == RB_KIND_HALT)
        st = ss->plat->halt ? ss->plat->halt() : RB_OK;
    else
        st = ss->plat->boot ? ss->plat->boot(t, &ss->handoff) : RB_OK;
    ss->phase = RB_PHASE_DONE;
    ss->running = 0;
    return st;
}

static void begin_boot_selected(rb_session *ss)
{
    ss->boot_index = ss->selected;
    ss->phase = RB_PHASE_HANDOFF;
}

int rb_session_tick(rb_session *ss)
{
    uint64_t now;
    float dt;
    int i;
    if (!ss || !ss->running) return 0;
    now = ss->plat->now_ms();
    if (ss->last_ms == 0) ss->last_ms = now;
    dt = (float)(now - ss->last_ms) / 1000.f;
    if (dt < 0.f) dt = 0.f;
    if (dt > 0.05f) dt = 0.05f;
    ss->last_ms = now;

    rb_zero(&ss->input, sizeof(ss->input));
    ss->plat->poll_input(&ss->input);
    if (ss->input.quit) {
        ss->running = 0;
        return 0;
    }
    if (picker_chord(ss)) ss->alt_latched = 1;

    if (ss->phase == RB_PHASE_LOAD || ss->phase == RB_PHASE_SPLASH) {
        if (!ss->splash_inited) {
            ss->splash_inited = 1;
            ss->splash_start_ms = now;
            ss->phase = RB_PHASE_SPLASH;
            if (ss->load_real < 1.f) {
                if (load_resources(ss) != RB_OK) {
                    ss->running = 0;
                    return 0;
                }
            }
        }
        {
            uint64_t elapsed = now - ss->splash_start_ms;
            float fade_in = rb_ease_apple(rb_clampf((float)elapsed / 600.f, 0.f, 1.f));
            ss->load_visual = rb_splash_visual(ss->load_real, elapsed, ss->catalog.splash_min_ms);
            ss->fade = fade_in;
            rb_splash_draw(ss, ss->load_visual, fade_in);
            if (ss->plat->present) ss->plat->present(&ss->fb);
            if (rb_splash_complete(ss->load_real, elapsed, ss->catalog.splash_min_ms)) {
                rb_boot_intent intent = rb_policy_intent(
                    ss->alt_latched, ss->force_menu, rb_env_get(&ss->env, "next_boot"));
                ss->splash_done = 1;
                if (intent == RB_BOOT_PICKER) {
                    ss->phase = RB_PHASE_PICKER;
                    ss->fade = 0.f;
                    ss->sel_anim = 1.f;
                    ss->cursor_on = 1;
                } else {
                    ss->selected = rb_policy_resolve_index(&ss->catalog, &ss->env, 0);
                    begin_boot_selected(ss);
                }
            }
        }
        return 1;
    }

    if (ss->phase == RB_PHASE_PICKER) {
        ss->fade = rb_clampf(ss->fade + dt / 0.600f, 0.f, 1.f);
        ss->sel_anim = rb_clampf(ss->sel_anim + dt / 0.300f, 0.f, 1.f);
        if (key_hit(&ss->input, RB_KEY_LEFT)) {
            ss->selected = rb_policy_next_index(&ss->catalog, ss->selected, -1);
            ss->sel_anim = 0.f;
            ss->timeout_left_ms = ss->catalog.picker_timeout_s * 1000;
        }
        if (key_hit(&ss->input, RB_KEY_RIGHT)) {
            ss->selected = rb_policy_next_index(&ss->catalog, ss->selected, 1);
            ss->sel_anim = 0.f;
            ss->timeout_left_ms = ss->catalog.picker_timeout_s * 1000;
        }
        consume_hotkey(ss);
        if (ss->input.mouse_pressed & RB_MOUSE_LEFT) {
            int hit = rb_picker_hit(ss, ss->input.mouse_x, ss->input.mouse_y);
            if (hit >= 0) {
                if (hit == ss->selected) begin_boot_selected(ss);
                else {
                    ss->selected = hit;
                    ss->sel_anim = 0.f;
                }
            }
        }
        if (key_hit(&ss->input, RB_KEY_ENTER)) begin_boot_selected(ss);
        if (key_hit(&ss->input, RB_KEY_F10) || key_hit(&ss->input, RB_KEY_CONSOLE)) {
            ss->phase = RB_PHASE_CONSOLE;
            ss->console_len = 0;
            ss->console_in[0] = 0;
        }
        if (ss->catalog.picker_timeout_s > 0) {
            ss->timeout_left_ms -= (int)(dt * 1000.f);
            if (ss->timeout_left_ms <= 0) {
                ss->selected = rb_policy_resolve_index(&ss->catalog, &ss->env, 0);
                begin_boot_selected(ss);
            }
        }
        rb_picker_draw(ss);
        if (ss->plat->present) ss->plat->present(&ss->fb);
        return 1;
    }

    if (ss->phase == RB_PHASE_AUTH) {
        for (i = 0; i < ss->input.nchars; i++) {
            int ch = ss->input.chars[i];
            if (ch >= 32 && ch < 127 && ss->auth_len < (int)sizeof(ss->auth_buf) - 1) {
                ss->auth_buf[ss->auth_len++] = (char)ch;
                ss->auth_buf[ss->auth_len] = 0;
                ss->auth_fail = 0;
            }
        }
        if (key_hit(&ss->input, RB_KEY_BACKSPACE) && ss->auth_len > 0) {
            ss->auth_buf[--ss->auth_len] = 0;
        }
        if (key_hit(&ss->input, RB_KEY_ESC)) {
            ss->phase = RB_PHASE_PICKER;
            ss->auth_len = 0;
        }
        if (key_hit(&ss->input, RB_KEY_ENTER)) {
            rb_target *t = &ss->catalog.targets[ss->selected];
            if (rb_lock_verify(t->lock, ss->auth_buf)) {
                t->restricted = 0;
                begin_boot_selected(ss);
            } else {
                ss->auth_fail = 1;
                ss->auth_len = 0;
                ss->auth_buf[0] = 0;
            }
        }
        rb_auth_draw(ss);
        if (ss->plat->present) ss->plat->present(&ss->fb);
        return 1;
    }

    if (ss->phase == RB_PHASE_CONSOLE) {
        for (i = 0; i < ss->input.nchars; i++) {
            int ch = ss->input.chars[i];
            if (ch >= 32 && ch < 127 && ss->console_len < (int)sizeof(ss->console_in) - 1) {
                ss->console_in[ss->console_len++] = (char)ch;
                ss->console_in[ss->console_len] = 0;
            }
        }
        if (key_hit(&ss->input, RB_KEY_BACKSPACE) && ss->console_len > 0) {
            ss->console_in[--ss->console_len] = 0;
        }
        if (key_hit(&ss->input, RB_KEY_ENTER)) rb_console_submit(ss);
        if (key_hit(&ss->input, RB_KEY_ESC)) ss->phase = RB_PHASE_PICKER;
        rb_console_draw(ss);
        if (ss->plat->present) ss->plat->present(&ss->fb);
        return 1;
    }

    if (ss->phase == RB_PHASE_HANDOFF) {
        try_boot(ss, ss->boot_index);
        return ss->running;
    }

    return ss->running;
}

rb_status rb_session_boot(rb_session *ss)
{
    while (rb_session_tick(ss)) {
        if (ss->plat->sleep_ms) ss->plat->sleep_ms(8);
    }
    return RB_OK;
}
