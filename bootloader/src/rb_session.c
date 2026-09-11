#include "roboboot.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void fmt_bytes(char *out, size_t cap, size_t n)
{
    if (!out || cap < 1) return;
    if (n >= 1024u * 1024u)
        snprintf(out, cap, "%zu.%zu MB", n / (1024u * 1024u), (n / 102400u) % 10u);
    else if (n >= 1024u)
        snprintf(out, cap, "%zu KB", n / 1024u);
    else
        snprintf(out, cap, "%zu bytes", n);
}

static rb_status load_path_image(rb_session *ss, const char *rel, rb_image *im)
{
    char path[RB_MAX_PATH];
    char bytes[32];
    void *data = NULL;
    size_t len = 0;
    rb_status st;
    rb_image_free(im);
    rb_join(path, sizeof(path), ss->plat->root, rel);
    rb_bootlogf(ss, "Read  %s", rel);
    st = ss->plat->read_file(path, &data, &len);
    if (st != RB_OK) {
        rb_bootlogf(ss, "Read  failed  %s  (%d)", rel, st);
        return st;
    }
    fmt_bytes(bytes, sizeof(bytes), len);
    rb_bootlogf(ss, "Read  %s  %s", rel, bytes);
    st = rb_image_load_mem(data, len, im);
    rb_free(data);
    if (st != RB_OK) {
        rb_bootlogf(ss, "RBIM  decode failed  %s  (%d)", rel, st);
        return st;
    }
    rb_bootlogf(ss, "RBIM  %s  %d x %d", rel, im->w, im->h);
    return st;
}

static rb_status load_path_font(rb_session *ss, const char *rel, rb_font *font)
{
    char path[RB_MAX_PATH];
    char bytes[32];
    void *data = NULL;
    size_t len = 0;
    rb_status st;
    rb_font_free(font);
    rb_join(path, sizeof(path), ss->plat->root, rel);
    rb_bootlogf(ss, "Read  %s", rel);
    st = ss->plat->read_file(path, &data, &len);
    if (st != RB_OK) {
        rb_bootlogf(ss, "Font  missing  %s  (%d)", rel, st);
        return st;
    }
    fmt_bytes(bytes, sizeof(bytes), len);
    rb_bootlogf(ss, "Read  %s  %s", rel, bytes);
    st = rb_font_load_mem(data, len, font);
    rb_free(data);
    if (st != RB_OK) {
        rb_bootlogf(ss, "Font  decode failed  %s  (%d)", rel, st);
        return st;
    }
    rb_bootlogf(ss, "Font  %s  %d px  glyphs %d-%d  atlas %dx%d",
                rel, font->px, font->first, font->last, font->atlas_w, font->atlas_h);
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
    rb_status st;
    int i, present = 0;

    bump_load(ss, 0.04f);
    rb_bootlogf(ss, "RoboBoot %s", RB_VERSION_STR);
    rb_bootlog(ss, "Load  catalog");
    rb_join(path, sizeof(path), ss->plat->root, "config/roboboot.manifest");
    rb_bootlogf(ss, "Open  %s", path[0] ? path : "config/roboboot.manifest");
    st = rb_manifest_load_path(path, &ss->catalog);
    if (st != RB_OK) {
        rb_bootlogf(ss, "Catalog  parse failed  (%d)", st);
        return st;
    }
    rb_bootlogf(ss, "Catalog  v%d  %d targets", ss->catalog.version, ss->catalog.ntargets);
    rb_bootlogf(ss, "Catalog  splash %d ms  timeout %d s",
                ss->catalog.splash_min_ms, ss->catalog.picker_timeout_s);
    rb_bootlogf(ss, "Catalog  default %s  fallback %s",
                ss->catalog.default_id[0] ? ss->catalog.default_id : "-",
                ss->catalog.fallback_id[0] ? ss->catalog.fallback_id : "-");
    if (ss->catalog.picker_hint[0])
        rb_bootlogf(ss, "Catalog  hint  %s", ss->catalog.picker_hint);
    bump_load(ss, 0.16f);

    rb_bootlog(ss, "Load  settings");
    rb_join(path, sizeof(path), ss->plat->root, ss->catalog.env_path);
    st = rb_env_load(path, &ss->env);
    rb_bootlogf(ss, "Settings  %s  (%d)", ss->catalog.env_path, st);
    {
        int k;
        const char *next = rb_env_get(&ss->env, "next_boot");
        const char *last = rb_env_get(&ss->env, "last_boot");
        rb_bootlogf(ss, "Settings  %d keys  debug %s",
                    ss->env.n, rb_debug_enabled(&ss->env) ? "on" : "off");
        for (k = 0; k < ss->env.n; k++) {
            if (ss->env.keys[k][0] == 0) continue;
            rb_bootlogf(ss, "Env  %s=%s", ss->env.keys[k], ss->env.vals[k]);
        }
        rb_bootlogf(ss, "Env  last %s  next %s",
                    last && last[0] ? last : "-",
                    next && next[0] ? next : "-");
    }
    bump_load(ss, 0.26f);

    rb_bootlog(ss, "Probe  volumes/");
    rb_join(path, sizeof(path), ss->plat->root, "volumes");
    rb_probe_volumes(path, &ss->probe);
    for (i = 0; i < ss->probe.nvols; i++) {
        rb_bootlogf(ss, "Volume  %s  kernel %s  efi %s",
                    ss->probe.vols[i].name,
                    ss->probe.vols[i].has_kernel ? "yes" : "no",
                    ss->probe.vols[i].has_efi ? "yes" : "no");
    }
    rb_probe_apply(&ss->catalog, &ss->probe);
    for (i = 0; i < ss->catalog.ntargets; i++) {
        rb_target *t = &ss->catalog.targets[i];
        if (t->present) present++;
        rb_bootlogf(ss, "Target  %s  %s  vol %s  %s%s%s",
                    t->id, rb_kind_name(t->kind),
                    t->volume[0] ? t->volume : "-",
                    t->present ? "ready" : "missing",
                    t->hidden ? "  hidden" : "",
                    t->restricted ? "  locked" : "");
        if (t->kernel[0])
            rb_bootlogf(ss, "  kernel  %s", t->kernel);
        if (t->ramdisk[0])
            rb_bootlogf(ss, "  initfs  %s", t->ramdisk);
        if (t->chain[0])
            rb_bootlogf(ss, "  chain  %s", t->chain);
        if (t->icon[0])
            rb_bootlogf(ss, "  picture  %s", t->icon);
        if (t->args[0])
            rb_bootlogf(ss, "  args  %s", t->args);
        if (t->hotkey)
            rb_bootlogf(ss, "  hotkey  %c", t->hotkey);
    }
    rb_bootlogf(ss, "Probe  %d folders  %d ready", ss->probe.nvols, present);
    bump_load(ss, 0.46f);

    rb_bootlogf(ss, "Logo  %s", ss->catalog.splash_logo[0] ? ss->catalog.splash_logo : "(none)");
    st = load_path_image(ss, ss->catalog.splash_logo, &ss->logo);
    if (st != RB_OK || !ss->logo.px)
        rb_bootlog(ss, "Logo  missing");
    bump_load(ss, 0.60f);

    rb_bootlog(ss, "Fonts");
    load_path_font(ss, "assets/fonts/ui-title.rbfn", &ss->font_title);
    load_path_font(ss, "assets/fonts/ui-body.rbfn", &ss->font_body);
    load_path_font(ss, "assets/fonts/ui-caption.rbfn", &ss->font_caption);
    bump_load(ss, 0.94f);

    ss->selected = rb_policy_resolve_index(&ss->catalog, &ss->env, 0);
    if (ss->selected < 0) ss->selected = 0;
    rb_bootlogf(ss, "Policy  default index %d  %s",
                ss->selected, ss->catalog.targets[ss->selected].id);
    ss->timeout_left_ms = ss->catalog.picker_timeout_s * 1000;
    rb_bootlogf(ss, "Splash  hold %d ms  picker timeout %d s",
                ss->catalog.splash_min_ms, ss->catalog.picker_timeout_s);
    rb_bootlog(ss, "Resources ready");
    bump_load(ss, 1.f);
    return RB_OK;
}

static void load_picker_icons(rb_session *ss)
{
    int i;
    if (!ss || ss->icons_loaded) return;
    rb_bootlog(ss, "Picker  load pictures");
    for (i = 0; i < ss->catalog.ntargets; i++) {
        if (ss->catalog.targets[i].icon[0]) {
            if (load_path_image(ss, ss->catalog.targets[i].icon, &ss->icons[i]) != RB_OK)
                rb_bootlogf(ss, "Picture  skip  %s", ss->catalog.targets[i].id);
        } else
            rb_bootlogf(ss, "Picture  none  %s", ss->catalog.targets[i].id);
    }
    ss->icons_loaded = 1;
    rb_bootlog(ss, "Picker  pictures ready");
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
    rb_bootlog(ss, "Session  start");
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

static int confirm_hit(const rb_input *in)
{
    int i;
    if (key_hit(in, RB_KEY_ENTER)) return 1;
    for (i = 0; i < in->nchars; i++)
        if (in->chars[i] == ' ') return 1;
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
        rb_bootlog(ss, "Auth  volume locked");
        ss->phase = RB_PHASE_AUTH;
        ss->auth_len = 0;
        ss->auth_buf[0] = 0;
        ss->auth_fail = 0;
        return RB_OK;
    }
    st = rb_loader_prepare(t, ss->plat->root, &ss->handoff);
    if (st != RB_OK) {
        rb_bootlogf(ss, "Prepare failed  %s  (%d)", t->id, st);
        idx = rb_policy_resolve_index(&ss->catalog, &ss->env, 1);
        if (idx < 0 || idx == ss->boot_index) return st;
        ss->boot_index = idx;
        ss->selected = idx;
        return try_boot(ss, idx);
    }
    ss->handoff.debug = rb_debug_enabled(&ss->env);
    if (rb_debug_enabled(&ss->env)) {
        float tx, ty, tw, th;
        rb_debug_term_geom(ss, &tx, &ty, &tw, &th);
        ss->handoff.debug_term_x = (int)tx;
        ss->handoff.debug_term_y = (int)ty;
        ss->handoff.debug_term_w = (int)tw;
        ss->handoff.debug_term_h = (int)th;
        rb_bootlogf(ss, "Debug  term  %d,%d  %dx%d",
                    ss->handoff.debug_term_x, ss->handoff.debug_term_y,
                    ss->handoff.debug_term_w, ss->handoff.debug_term_h);
    }
    rb_bootlog(ss, "Handoff  ExitBootServices next");
    if (rb_debug_enabled(&ss->env))
        rb_debug_flush(ss);
    if (ss->catalog.save_last) {
        rb_bootlogf(ss, "Env  save last_boot=%s", t->id);
        rb_env_set(&ss->env, "last_boot", t->id);
        rb_env_set(&ss->env, "next_boot", "");
        if (rb_env_save(&ss->env) != RB_OK)
            rb_bootlog(ss, "Env  save failed");
        else
            rb_bootlog(ss, "Env  saved");
    }
    if (t->kind == RB_KIND_FIRMWARE) {
        rb_bootlog(ss, "Jump  firmware setup");
        st = ss->plat->firmware_setup ? ss->plat->firmware_setup() : RB_OK;
    } else if (t->kind == RB_KIND_REBOOT) {
        rb_bootlog(ss, "Jump  reboot");
        st = ss->plat->reboot ? ss->plat->reboot() : RB_OK;
    } else if (t->kind == RB_KIND_HALT) {
        rb_bootlog(ss, "Jump  halt");
        st = ss->plat->halt ? ss->plat->halt() : RB_OK;
    } else {
        rb_bootlog(ss, "Jump  kernel");
        st = ss->plat->boot ? ss->plat->boot(t, &ss->handoff) : RB_OK;
    }
    /* Host cannot jump into the ELF; keep the window on the kernel log.
       EFI never returns from a successful efi_start_kernel(). */
    if (st == RB_OK &&
        (t->kind == RB_KIND_KERNEL || t->kind == RB_KIND_LINUX || t->kind == RB_KIND_MULTIBOOT2)) {
        if (rb_debug_enabled(&ss->env)) {
            rb_bootlog(ss, "Kernel entered");
            rb_bootlogf(ss, "Framebuffer  %d x %d  32-bit", ss->fb.w, ss->fb.h);
            rb_bootlog(ss, "Memory  host map");
            rb_bootlog(ss, "CPU  x86_64 idle");
        }
        ss->phase = RB_PHASE_KLOG;
        ss->running = 1;
        return st;
    }
    ss->phase = RB_PHASE_DONE;
    ss->running = 0;
    return st;
}

static void begin_boot_selected(rb_session *ss)
{
    ss->boot_index = ss->selected;
    rb_bootlogf(ss, "Boot  %s  %s", ss->catalog.targets[ss->selected].id,
                rb_kind_name(ss->catalog.targets[ss->selected].kind));
    if (rb_debug_enabled(&ss->env)) {
        rb_bootlog(ss, "Phase  verbose hold");
        ss->phase = RB_PHASE_VERBOSE;
        ss->timeout_left_ms = 900;
        return;
    }
    /* Leave the picker immediately so a quiet kernel does not freeze the menu. */
    ss->splash_bg_ready = 0;
    ss->fb.damage_w = 0;
    ss->fb.damage_h = 0;
    rb_splash_draw(ss, 1.f, 1.f);
    if (ss->plat->present) ss->plat->present(&ss->fb);
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
    if (picker_chord(ss)) {
        if (!ss->alt_latched)
            rb_bootlog(ss, "Alt+M detected");
        ss->alt_latched = 1;
    }
    if (ss->phase == RB_PHASE_LOAD || ss->phase == RB_PHASE_SPLASH) {
        if (key_hit(&ss->input, RB_KEY_ESC) || key_hit(&ss->input, RB_KEY_F8) ||
            key_hit(&ss->input, RB_KEY_F10)) {
            if (!ss->alt_latched)
                rb_bootlog(ss, "Picker key");
            ss->alt_latched = 1;
        }
    }

    if (ss->phase != RB_PHASE_LOAD && ss->phase != RB_PHASE_SPLASH) {
        ss->fb.damage_w = 0;
        ss->fb.damage_h = 0;
    }

    if (ss->phase == RB_PHASE_LOAD || ss->phase == RB_PHASE_SPLASH) {
        if (!ss->splash_inited) {
            ss->splash_inited = 1;
            ss->phase = RB_PHASE_SPLASH;
            if (ss->load_real < 1.f) {
                if (load_resources(ss) != RB_OK) {
                    ss->running = 0;
                    return 0;
                }
            }
            now = ss->plat->now_ms();
            ss->splash_start_ms = now;
            ss->last_ms = now;
            rb_bootlogf(ss, "Splash  timer  %d ms  display %d x %d",
                        ss->catalog.splash_min_ms, ss->fb.w, ss->fb.h);
        }
        {
            uint64_t elapsed = now - ss->splash_start_ms;
            int done;
            ss->load_visual = rb_splash_visual(ss->load_real, elapsed, ss->catalog.splash_min_ms);
            ss->fade = 1.f;
            done = rb_splash_complete(ss->load_real, elapsed, ss->catalog.splash_min_ms);
            if (!done && ss->load_real >= 0.999f && ss->splash_ticks >= 80)
                done = 1;
            if (done)
                ss->load_visual = 1.f;
            rb_splash_draw(ss, ss->load_visual, 1.f);
            if (ss->plat->present) ss->plat->present(&ss->fb);
            ss->splash_ticks++;
            if (done) {
                rb_boot_intent intent = rb_policy_intent(
                    ss->alt_latched, ss->force_menu, rb_env_get(&ss->env, "next_boot"));
                ss->splash_done = 1;
                rb_bootlogf(ss, "Splash  done  %llu ms  intent %s",
                            (unsigned long long)elapsed,
                            intent == RB_BOOT_PICKER ? "picker" :
                            intent == RB_BOOT_ONCE ? "next_boot" : "default");
                if (ss->alt_latched)
                    rb_bootlog(ss, "Splash  picker chord latched");
                if (ss->force_menu)
                    rb_bootlog(ss, "Splash  menu flag");
                if (intent == RB_BOOT_PICKER) {
                    rb_bootlog(ss, "Opening picker");
                    load_picker_icons(ss);
                    ss->phase = RB_PHASE_PICKER;
                    ss->fade = 1.f;
                    ss->sel_anim = 1.f;
                    ss->picker_drawn = 0;
                } else {
                    ss->selected = rb_policy_resolve_index(&ss->catalog, &ss->env, 0);
                    begin_boot_selected(ss);
                }
            }
        }
        return 1;
    }

    if (ss->phase == RB_PHASE_PICKER) {
        int prev = ss->selected;
        int dirty;
        ss->fade = 1.f;
        ss->sel_anim = 1.f;
        if (key_hit(&ss->input, RB_KEY_LEFT)) {
            ss->selected = rb_policy_next_index(&ss->catalog, ss->selected, -1);
            ss->timeout_left_ms = ss->catalog.picker_timeout_s * 1000;
            rb_bootlogf(ss, "Selected  %s", ss->catalog.targets[ss->selected].id);
        }
        if (key_hit(&ss->input, RB_KEY_RIGHT)) {
            ss->selected = rb_policy_next_index(&ss->catalog, ss->selected, 1);
            ss->timeout_left_ms = ss->catalog.picker_timeout_s * 1000;
            rb_bootlogf(ss, "Selected  %s", ss->catalog.targets[ss->selected].id);
        }
        consume_hotkey(ss);
        for (i = 0; i < ss->input.nchars; i++) {
            int ch = ss->input.chars[i];
            if (ch == 's' || ch == 'S') {
                rb_bootlog(ss, "Settings");
                ss->phase = RB_PHASE_SETTINGS;
                ss->fade = 1.f;
                ss->settings_drawn = 0;
                break;
            }
        }
        if (confirm_hit(&ss->input)) begin_boot_selected(ss);
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
        if (ss->phase != RB_PHASE_PICKER) return 1;
        dirty = !ss->picker_drawn || ss->selected != prev;
        if (rb_debug_enabled(&ss->env) && ss->boot_log_seq != ss->boot_log_shown) dirty = 1;
        if (ss->catalog.picker_timeout_s > 0) {
            int now_sec = (ss->timeout_left_ms + 999) / 1000;
            int was_sec = (ss->timeout_left_ms + (int)(dt * 1000.f) + 999) / 1000;
            if (now_sec != was_sec) dirty = 1;
        }
        if (dirty) {
            rb_picker_draw(ss);
            if (ss->plat->present) ss->plat->present(&ss->fb);
            ss->picker_drawn = 1;
        }
        return 1;
    }

    if (ss->phase == RB_PHASE_SETTINGS) {
        int dirty = !ss->settings_drawn;
        ss->fade = 1.f;
        if (key_hit(&ss->input, RB_KEY_ESC)) {
            rb_env_save(&ss->env);
            ss->phase = RB_PHASE_PICKER;
            ss->picker_drawn = 0;
            ss->fade = 1.f;
            return 1;
        }
        if (key_hit(&ss->input, RB_KEY_ENTER) || key_hit(&ss->input, RB_KEY_RIGHT) ||
            key_hit(&ss->input, RB_KEY_LEFT)) {
            rb_debug_set(&ss->env, !rb_debug_enabled(&ss->env));
            rb_bootlog(ss, rb_debug_enabled(&ss->env) ? "Debug on" : "Debug off");
            if (rb_env_save(&ss->env) != RB_OK)
                rb_logf("debug setting not saved");
            dirty = 1;
        }
        if (dirty) {
            rb_settings_draw(ss);
            if (ss->plat->present) ss->plat->present(&ss->fb);
            ss->settings_drawn = 1;
        }
        return 1;
    }

    if (ss->phase == RB_PHASE_VERBOSE) {
        int dirty;
        ss->timeout_left_ms -= (int)(dt * 1000.f);
        dirty = !ss->splash_bg_ready ||
                (rb_debug_enabled(&ss->env) && ss->boot_log_seq != ss->boot_log_shown);
        if (dirty) {
            rb_verbose_draw(ss);
            if (ss->plat->present) ss->plat->present(&ss->fb);
        }
        if (ss->timeout_left_ms <= 0 || key_hit(&ss->input, RB_KEY_ENTER))
            ss->phase = RB_PHASE_HANDOFF;
        return 1;
    }

    if (ss->phase == RB_PHASE_KLOG) {
        int dirty;
        if (key_hit(&ss->input, RB_KEY_ESC)) {
            ss->running = 0;
            return 0;
        }
        dirty = !ss->splash_bg_ready || ss->boot_log_seq != ss->boot_log_shown;
        if (dirty) {
            rb_klog_draw(ss);
            if (ss->plat->present) ss->plat->present(&ss->fb);
        }
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
            ss->picker_drawn = 0;
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
        if (key_hit(&ss->input, RB_KEY_ESC)) {
            ss->phase = RB_PHASE_PICKER;
            ss->picker_drawn = 0;
        }
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
        if (ss->plat->sleep_ms) ss->plat->sleep_ms(32);
    }
    return RB_OK;
}
