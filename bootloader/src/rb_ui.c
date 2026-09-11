#include "roboboot.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static rb_session *g_bootlog_ss;
static char g_early_log[96][RB_BOOTLOG_COLS];
static uint32_t g_early_ms[96];
static uint8_t g_early_kind[96];
static int g_early_n;

static int visible_count(const rb_catalog *c)
{
    int i, n = 0;
    for (i = 0; i < c->ntargets; i++)
        if (c->targets[i].present && !c->targets[i].hidden) n++;
    return n;
}

static int nth_visible(const rb_catalog *c, int n)
{
    int i;
    for (i = 0; i < c->ntargets; i++) {
        if (c->targets[i].present && !c->targets[i].hidden) {
            if (n == 0) return i;
            n--;
        }
    }
    return 0;
}

static int visible_rank(const rb_catalog *c, int idx)
{
    int i, n = 0;
    for (i = 0; i < c->ntargets; i++) {
        if (c->targets[i].present && !c->targets[i].hidden) {
            if (i == idx) return n;
            n++;
        }
    }
    return 0;
}

static const rb_font *ui_caption(const rb_session *ss)
{
    return ss->font_caption.atlas ? &ss->font_caption : &ss->font_body;
}

static const rb_font *ui_body(const rb_session *ss)
{
    return ss->font_body.atlas ? &ss->font_body : ui_caption(ss);
}

static int ui_compact(const rb_session *ss)
{
    return ss->fb.w <= 734;
}

static float ui_edge(const rb_session *ss)
{
    if (ss->fb.w <= 734) return 22.f;
    if (ss->fb.w <= 1068) return 40.f;
    {
        float p = ss->fb.w * 0.05f;
        return p > 80.f ? 80.f : p;
    }
}

static float ui_pic_gap(const rb_session *ss)
{
    return ui_compact(ss) ? 16.f : 32.f;
}

static float ui_pic_h_base(const rb_session *ss)
{
    return ui_compact(ss) ? 112.f : 200.f;
}

static const float UI_HALO_PAD = 16.f;

static const rb_font *ui_label_font(const rb_session *ss)
{
    return ui_compact(ss) ? ui_caption(ss) : &ss->font_body;
}

static float ui_col_w(const rb_session *ss, int vis_index, float pic_w)
{
    int idx = nth_visible(&ss->catalog, vis_index);
    int tw = rb_gfx_text_width(ui_label_font(ss), ss->catalog.targets[idx].title) + 12;
    return pic_w > (float)tw ? pic_w : (float)tw;
}

static void ui_pic_wh(const rb_image *im, float ih, float *iw, float *oh)
{
    float aspect = 1.f;
    if (im && im->h > 0) aspect = (float)im->w / (float)im->h;
    *oh = ih;
    *iw = ih * aspect;
}

static int splash_logo_h(const rb_session *ss)
{
    return ui_compact(ss) ? 128 : 256;
}

static int ui_debug_on(const rb_session *ss)
{
    return ss && rb_debug_enabled(&ss->env);
}

void rb_debug_term_geom(const rb_session *ss, float *x, float *y, float *w, float *h)
{
    float pad, th;
    if (!ss) return;
    pad = ui_edge(ss);
    th = ui_compact(ss) ? 188.f : 280.f;
    if (th > (float)ss->fb.h * 0.42f) th = (float)ss->fb.h * 0.42f;
    if (th < 120.f) th = 120.f;
    if (x) *x = pad;
    if (w) *w = (float)ss->fb.w - pad * 2.f;
    if (h) *h = th;
    if (y) *y = (float)ss->fb.h - pad - th;
}

static float ui_debug_reserve(const rb_session *ss)
{
    float x, y, w, h;
    if (!ui_debug_on(ss)) return 0.f;
    rb_debug_term_geom(ss, &x, &y, &w, &h);
    return ((float)ss->fb.h - y) + 16.f;
}

static void fit_splash_logo(rb_session *ss)
{
    int lh, lw;
    rb_image scaled;
    if (!ss || ss->logo_fitted || !ss->logo.px || ss->logo.h < 1) {
        if (ss) ss->logo_fitted = 1;
        return;
    }
    lh = splash_logo_h(ss);
    lw = (int)((lh * (float)ss->logo.w / (float)ss->logo.h) + 0.5f);
    if (lw < 1) lw = 1;
    if (ss->logo.w == lw && ss->logo.h == lh) {
        ss->logo_fitted = 1;
        return;
    }
    rb_zero(&scaled, sizeof(scaled));
    if (rb_image_resize(&ss->logo, lw, lh, &scaled) == RB_OK) {
        rb_image_free(&ss->logo);
        ss->logo = scaled;
    }
    ss->logo_fitted = 1;
}

void rb_splash_draw(rb_session *ss, float visual, float fade)
{
    float cx, lw, lh, lx, ly, bar_w, bar_h, bar_x, bar_y, fill, stack, top;
    int bx, by, bw, bh;
    uint32_t bg, track, brand;
    if (!ss) return;
    (void)fade;
    visual = rb_clampf(visual, 0.f, 1.f);
    fit_splash_logo(ss);
    cx = ss->fb.w * 0.5f;
    lh = (float)splash_logo_h(ss);
    lw = lh;
    if (ss->logo.px && ss->logo.h > 0) {
        lw = (float)ss->logo.w;
        lh = (float)ss->logo.h;
    }
    bar_w = ui_compact(ss) ? 240.f : 280.f;
    bar_h = 4.f;
    stack = lh + 64.f + bar_h;
    {
        float area = (float)ss->fb.h - ui_debug_reserve(ss);
        top = area * 0.5f - stack * 0.5f;
        if (top < 24.f) top = 24.f;
    }
    lx = cx - lw * 0.5f;
    ly = top;
    bar_x = cx - bar_w * 0.5f;
    bar_y = ly + lh + 64.f;
    bx = (int)bar_x;
    by = (int)bar_y;
    bw = (int)bar_w;
    bh = (int)bar_h;
    if (bh < 4) bh = 4;
    fill = bar_w * visual;
    if (fill > 0.f && fill < bar_h) fill = bar_h;
    bg = rb_token(RB_TOKEN_BG_PRIMARY, 255);
    track = rb_token(RB_TOKEN_BORDER_DEFAULT, 255);
    brand = rb_token(RB_TOKEN_BRAND_PRIMARY, 255);

    if (!ss->splash_bg_ready) {
        rb_gfx_clear(&ss->fb, bg);
        if (ss->logo.px)
            rb_gfx_blit(&ss->fb, &ss->logo, lx, ly, lw, lh, 1.f);
        else
            rb_gfx_round_rect(&ss->fb, lx, ly, lw, lh, 18.f, brand);
        rb_gfx_fill_box(&ss->fb, bx - 2, by - 2, bw + 4, bh + 4, bg);
        rb_gfx_round_rect(&ss->fb, bar_x, bar_y, bar_w, bar_h, bar_h * 0.5f, track);
        if (fill > 0.f)
            rb_gfx_round_rect(&ss->fb, bar_x, bar_y, fill, bar_h, bar_h * 0.5f, brand);
        if (ui_debug_on(ss))
            rb_debug_term_draw(ss);
        ss->splash_bg_ready = 1;
        ss->fb.damage_w = 0;
        ss->fb.damage_h = 0;
        return;
    }

    rb_gfx_fill_box(&ss->fb, bx - 2, by - 2, bw + 4, bh + 4, bg);
    rb_gfx_round_rect(&ss->fb, bar_x, bar_y, bar_w, bar_h, bar_h * 0.5f, track);
    if (fill > 0.f)
        rb_gfx_round_rect(&ss->fb, bar_x, bar_y, fill, bar_h, bar_h * 0.5f, brand);
    if (ui_debug_on(ss) && ss->boot_log_seq != ss->boot_log_shown) {
        rb_debug_term_draw(ss);
        ss->fb.damage_w = 0;
        ss->fb.damage_h = 0;
        return;
    }
    ss->fb.damage_x = bx - 4;
    ss->fb.damage_y = by - 4;
    ss->fb.damage_w = bw + 8;
    ss->fb.damage_h = bh + 8;
}

static void icon_layout(const rb_session *ss, int vis_index, float *ox, float *oy, float *iw, float *ih)
{
    int n = visible_count(&ss->catalog);
    int i, pass;
    float gap = ui_pic_gap(ss);
    float h = ui_pic_h_base(ss);
    float edge = ui_edge(ss);
    float picw[RB_MAX_TARGETS];
    float colw[RB_MAX_TARGETS];
    float total = 0.f;
    float avail;
    float x;
    if (n < 1) n = 1;
    if (n > RB_MAX_TARGETS) n = RB_MAX_TARGETS;
    avail = (float)ss->fb.w - edge * 2.f;
    if (avail < 64.f) avail = 64.f;
    for (pass = 0; pass < 8; pass++) {
        total = 0.f;
        for (i = 0; i < n; i++) {
            int idx = nth_visible(&ss->catalog, i);
            float w, ph;
            ui_pic_wh(&ss->icons[idx], h, &w, &ph);
            picw[i] = w;
            colw[i] = ui_col_w(ss, i, w);
            total += colw[i];
            if (i + 1 < n) total += gap;
        }
        if (total <= avail || h <= 48.f) break;
        h *= avail / total;
        if (h < 48.f) h = 48.f;
    }
    x = ss->fb.w * 0.5f - total * 0.5f;
    if (vis_index < 0) vis_index = 0;
    if (vis_index >= n) vis_index = n - 1;
    for (i = 0; i < vis_index; i++)
        x += colw[i] + gap;
    *iw = picw[vis_index];
    *ih = h;
    *ox = x + colw[vis_index] * 0.5f;
    *oy = ((float)ss->fb.h - ui_debug_reserve(ss)) * 0.5f - 24.f;
}

static void fit_picker_icons(rb_session *ss)
{
    int i;
    float x, y, w, h;
    int th;
    if (!ss || ss->icons_fitted) return;
    if (visible_count(&ss->catalog) < 1) {
        ss->icons_fitted = 1;
        return;
    }
    icon_layout(ss, 0, &x, &y, &w, &h);
    th = (int)(h + 0.5f);
    if (th < 32) th = 32;
    for (i = 0; i < ss->catalog.ntargets; i++) {
        rb_image scaled;
        int tw;
        if (!ss->icons[i].px || ss->icons[i].h < 1) continue;
        if (ss->icons[i].h == th) continue;
        tw = (int)((h * (float)ss->icons[i].w / (float)ss->icons[i].h) + 0.5f);
        if (tw < 1) tw = 1;
        rb_zero(&scaled, sizeof(scaled));
        if (rb_image_resize(&ss->icons[i], tw, th, &scaled) == RB_OK) {
            rb_image_free(&ss->icons[i]);
            ss->icons[i] = scaled;
        }
    }
    ss->icons_fitted = 1;
}

void rb_picker_draw(rb_session *ss)
{
    int n, rank;
    if (!ss) return;
    fit_picker_icons(ss);
    ss->fade = 1.f;
    ss->sel_anim = 1.f;
    rb_gfx_clear(&ss->fb, rb_token(RB_TOKEN_BG_PRIMARY, 255));
    n = visible_count(&ss->catalog);
    ss->fb.damage_w = 0;
    ss->fb.damage_h = 0;

    for (rank = 0; rank < n; rank++) {
        int idx = nth_visible(&ss->catalog, rank);
        const rb_target *t = &ss->catalog.targets[idx];
        float cx, cy, iw, ih;
        int sel;
        icon_layout(ss, rank, &cx, &cy, &iw, &ih);
        sel = (idx == ss->selected);

        if (sel) {
            float pad = UI_HALO_PAD;
            float hx = cx - iw * 0.5f - pad;
            float hy = cy - ih * 0.5f - pad;
            float hw = iw + pad * 2.f;
            float hh = ih + pad * 2.f;
            rb_gfx_round_rect(&ss->fb, hx, hy, hw, hh, 18.f,
                              rb_tokenf(RB_TOKEN_BRAND_PRIMARY, RB_TOKEN_BRAND_SUBTLE_A));
            rb_gfx_round_rect_stroke(&ss->fb, hx, hy, hw, hh, 18.f, 2.f,
                                     rb_token(RB_TOKEN_BRAND_PRIMARY, 255));
        }

        if (ss->icons[idx].px) {
            float pw = (float)ss->icons[idx].w;
            float ph = (float)ss->icons[idx].h;
            rb_gfx_blit(&ss->fb, &ss->icons[idx], cx - pw * 0.5f, cy - ph * 0.5f, pw, ph, 1.f);
        } else
            rb_gfx_round_rect(&ss->fb, cx - iw * 0.5f, cy - ih * 0.5f, iw, ih, 18.f,
                              rb_token(RB_TOKEN_BG_TERTIARY, 255));

        rb_gfx_text_center(&ss->fb, ui_label_font(ss), cx, cy + ih * 0.5f + 24.f, t->title,
                           rb_token(sel ? RB_TOKEN_TEXT_PRIMARY : RB_TOKEN_TEXT_TERTIARY, 255));
    }

    {
        float hint_y = (float)ss->fb.h - 48.f;
        if (ui_debug_on(ss))
            hint_y = (float)ss->fb.h - ui_debug_reserve(ss) - 8.f;
        rb_gfx_text_center(&ss->fb, ui_caption(ss), ss->fb.w * 0.5f, hint_y,
                          ss->catalog.picker_hint[0] ? ss->catalog.picker_hint : "Choose a startup volume",
                          rb_token(RB_TOKEN_TEXT_TERTIARY, 255));
        rb_gfx_text(&ss->fb, ui_caption(ss), ui_edge(ss), hint_y, "Settings",
                    rb_token(RB_TOKEN_TEXT_TERTIARY, 255));
        if (ui_debug_on(ss))
            rb_debug_term_draw(ss);
    }

    if (ss->catalog.picker_timeout_s > 0 && ss->timeout_left_ms > 0) {
        char buf[64];
        float ty = (float)ss->fb.h - 64.f;
        if (ui_debug_on(ss))
            ty = (float)ss->fb.h - ui_debug_reserve(ss) - 24.f;
        snprintf(buf, sizeof(buf), "Booting in %d", (ss->timeout_left_ms + 999) / 1000);
        rb_gfx_text_center(&ss->fb, ui_caption(ss), ss->fb.w * 0.5f, ty, buf,
                           rb_token(RB_TOKEN_TEXT_TERTIARY, 255));
    }
    rb_fb_damage_full(&ss->fb);
}

void rb_auth_draw(rb_session *ss)
{
    float cx, cy, w, h, field_w, field_h, card_x, card_y, field_x, field_y;
    char dots[72];
    int i;
    if (!ss) return;
    rb_picker_draw(ss);
    rb_gfx_fill_rect(&ss->fb, 0.f, 0.f, (float)ss->fb.w, (float)ss->fb.h,
                     rb_token(RB_TOKEN_BG_PRIMARY, 178));
    cx = ss->fb.w * 0.5f;
    cy = ss->fb.h * 0.5f;
    w = 320.f;
    h = 160.f;
    field_w = w - 64.f;
    field_h = 44.f;
    {
        float pcx, pcy, piw, pih;
        icon_layout(ss, visible_rank(&ss->catalog, ss->selected), &pcx, &pcy, &piw, &pih);
        cy = pcy + pih * 0.5f + 64.f + h * 0.5f;
        if (ui_compact(ss) || cy + h * 0.5f > (float)ss->fb.h - 32.f)
            cy = (float)ss->fb.h * 0.5f;
    }
    card_x = cx - w * 0.5f;
    card_y = cy - h * 0.5f;
    field_x = cx - field_w * 0.5f;
    field_y = card_y + h - 32.f - field_h;
    rb_gfx_round_rect(&ss->fb, card_x, card_y + 8.f, w, h, 18.f,
                      rb_rgba(0, 0, 0, 90));
    rb_gfx_round_rect(&ss->fb, card_x, card_y, w, h, 18.f,
                      rb_token(RB_TOKEN_BG_ELEVATED, 255));
    rb_gfx_text_center(&ss->fb, &ss->font_body, cx, card_y + 32.f,
                      ss->auth_fail ? "Incorrect password" : "This volume is locked",
                      rb_token(ss->auth_fail ? RB_TOKEN_DANGER : RB_TOKEN_TEXT_SECONDARY, 255));
    for (i = 0; i < ss->auth_len && i < 24; i++) dots[i] = '*';
    dots[i] = 0;
    rb_gfx_round_rect_stroke(&ss->fb, field_x - 2.f, field_y - 2.f, field_w + 4.f, field_h + 4.f, 10.f, 2.f,
                             rb_tokenf(RB_TOKEN_BRAND_PRIMARY, RB_TOKEN_BRAND_FOCUS_A));
    rb_gfx_round_rect(&ss->fb, field_x, field_y, field_w, field_h, 8.f,
                      rb_token(RB_TOKEN_BG_TERTIARY, 255));
    rb_gfx_text_center(&ss->fb, &ss->font_body, cx, field_y + 14.f, dots[0] ? dots : "",
                      rb_token(RB_TOKEN_TEXT_PRIMARY, 255));
    rb_fb_damage_full(&ss->fb);
}

void rb_console_print(rb_session *ss, const char *line)
{
    int i;
    if (!ss || !line) return;
    if (ss->console_lines >= 16) {
        for (i = 1; i < 16; i++) rb_strcpy(ss->console_log[i - 1], sizeof(ss->console_log[0]), ss->console_log[i]);
        ss->console_lines = 15;
    }
    rb_strcpy(ss->console_log[ss->console_lines++], sizeof(ss->console_log[0]), line);
}

void rb_console_draw(rb_session *ss)
{
    int i;
    char prompt[280];
    float pad = 48.f;
    const rb_font *cap;
    if (!ss) return;
    cap = ui_caption(ss);
    pad = ui_compact(ss) ? 24.f : 48.f;
    rb_gfx_clear(&ss->fb, rb_token(RB_TOKEN_BG_PRIMARY, 255));
    rb_gfx_text(&ss->fb, &ss->font_title, pad, pad, "Recovery",
                rb_token(RB_TOKEN_TEXT_PRIMARY, 255));
    rb_gfx_text(&ss->fb, cap, pad, pad + 40.f, "Type help for commands.",
                rb_token(RB_TOKEN_TEXT_TERTIARY, 255));
    rb_gfx_fill_rect(&ss->fb, pad, pad + 64.f, (float)ss->fb.w - pad * 2.f, 1.f,
                     rb_token(RB_TOKEN_BORDER_DEFAULT, 255));
    for (i = 0; i < ss->console_lines; i++) {
        rb_gfx_text(&ss->fb, &ss->font_body, pad, pad + 88.f + i * 24.f, ss->console_log[i],
                    rb_token(RB_TOKEN_TEXT_SECONDARY, 255));
    }
    snprintf(prompt, sizeof(prompt), "rb> %s", ss->console_in);
    rb_gfx_text(&ss->fb, &ss->font_body, pad, (float)ss->fb.h - 48.f, prompt,
                rb_token(RB_TOKEN_TEXT_PRIMARY, 255));
    rb_fb_damage_full(&ss->fb);
}

static void cmd_help(rb_session *ss)
{
    rb_console_print(ss, "boot [id]     start a catalog target");
    rb_console_print(ss, "ls            list targets and volumes");
    rb_console_print(ss, "set k v       write environment");
    rb_console_print(ss, "debug on|off  verbose boot logs");
    rb_console_print(ss, "settings      open Settings");
    rb_console_print(ss, "env           show saved environment");
    rb_console_print(ss, "probe         rescan volumes");
    rb_console_print(ss, "exit          return to picker");
}

static void cmd_ls(rb_session *ss)
{
    int i;
    char line[240];
    for (i = 0; i < ss->catalog.ntargets; i++) {
        const rb_target *t = &ss->catalog.targets[i];
        snprintf(line, sizeof(line), "%s  %-12s  %-10s  %s%s",
                 i == ss->selected ? "*" : " ",
                 t->id, rb_kind_name(t->kind), t->title,
                 t->present ? "" : "  [missing]");
        rb_console_print(ss, line);
    }
    for (i = 0; i < ss->probe.nvols; i++) {
        snprintf(line, sizeof(line), "vol %-12s  %s", ss->probe.vols[i].name, ss->probe.vols[i].path);
        rb_console_print(ss, line);
    }
}

void rb_console_submit(rb_session *ss)
{
    char buf[256];
    char *cmd, *arg;
    rb_strcpy(buf, sizeof(buf), ss->console_in);
    ss->console_in[0] = 0;
    ss->console_len = 0;
    cmd = buf;
    while (*cmd == ' ') cmd++;
    if (!cmd[0]) return;
    arg = cmd;
    while (*arg && *arg != ' ') arg++;
    if (*arg) {
        *arg = 0;
        arg++;
        while (*arg == ' ') arg++;
    } else {
        arg = "";
    }

    {
        char shown[240];
        snprintf(shown, sizeof(shown), "> %s %s", cmd, arg);
        rb_console_print(ss, shown);
    }

    if (rb_str_eq(cmd, "help")) cmd_help(ss);
    else if (rb_str_eq(cmd, "exit") || rb_str_eq(cmd, "quit")) ss->phase = RB_PHASE_PICKER;
    else if (rb_str_eq(cmd, "ls")) cmd_ls(ss);
    else if (rb_str_eq(cmd, "env")) {
        int i;
        for (i = 0; i < ss->env.n; i++) {
            char line[240];
            snprintf(line, sizeof(line), "%s=%s", ss->env.keys[i], ss->env.vals[i]);
            rb_console_print(ss, line);
        }
    } else if (rb_str_eq(cmd, "set") && arg[0]) {
        char key[64];
        char *sp = arg;
        while (*sp && *sp != ' ') sp++;
        if (*sp) {
            size_t n = (size_t)(sp - arg);
            if (n >= sizeof(key)) n = sizeof(key) - 1;
            memcpy(key, arg, n);
            key[n] = 0;
            while (*sp == ' ') sp++;
            rb_env_set(&ss->env, key, sp);
            rb_env_save(&ss->env);
            rb_console_print(ss, "saved");
        }
    } else if (rb_str_eq(cmd, "debug")) {
        int on = 1;
        if (arg[0] && (rb_str_eq(arg, "0") || rb_str_eq(arg, "off") || rb_str_eq(arg, "false"))) on = 0;
        rb_debug_set(&ss->env, on);
        rb_env_save(&ss->env);
        rb_console_print(ss, on ? "debug on" : "debug off");
    } else if (rb_str_eq(cmd, "settings")) {
        ss->phase = RB_PHASE_SETTINGS;
        ss->fade = 0.f;
    } else if (rb_str_eq(cmd, "probe")) {
        char volroot[RB_MAX_PATH];
        rb_join(volroot, sizeof(volroot), ss->plat->root, "volumes");
        rb_probe_volumes(volroot, &ss->probe);
        rb_probe_apply(&ss->catalog, &ss->probe);
        rb_console_print(ss, "volumes rescanned");
    } else if (rb_str_eq(cmd, "boot")) {
        int idx = arg[0] ? rb_catalog_find(&ss->catalog, arg) : ss->selected;
        if (idx < 0) rb_console_print(ss, "unknown target");
        else {
            ss->selected = idx;
            ss->boot_index = idx;
            ss->phase = RB_PHASE_HANDOFF;
        }
    } else if (rb_str_eq(cmd, "reboot")) {
        if (ss->plat->reboot) ss->plat->reboot();
        ss->running = 0;
    } else if (rb_str_eq(cmd, "halt")) {
        if (ss->plat->halt) ss->plat->halt();
        ss->running = 0;
    } else if (rb_str_eq(cmd, "firmware")) {
        if (ss->plat->firmware_setup) ss->plat->firmware_setup();
        ss->running = 0;
    } else {
        rb_console_print(ss, "unknown command - try help");
    }
}

int rb_picker_visible_rank(const rb_catalog *c, int idx)
{
    return visible_rank(c, idx);
}

static int log_has(const char *s, const char *n)
{
    int i, j;
    if (!s || !n || !n[0]) return 0;
    for (i = 0; s[i]; i++) {
        for (j = 0; n[j] && s[i + j] && s[i + j] == n[j]; j++) { }
        if (!n[j]) return 1;
    }
    return 0;
}

static uint8_t bootlog_kind(const char *line)
{
    if (log_has(line, "fail") || log_has(line, "Fail") || log_has(line, "missing") ||
        log_has(line, "Missing") || log_has(line, "not saved") || log_has(line, "skip"))
        return 2;
    if (log_has(line, "ready") || log_has(line, "loaded") || log_has(line, "Loaded") ||
        log_has(line, "detected") || log_has(line, "mapped") || log_has(line, "ok") ||
        log_has(line, "found") || log_has(line, "copied") || log_has(line, "jump"))
        return 1;
    return 0;
}

static uint64_t g_log_origin;
static int g_log_origin_set;

static uint32_t bootlog_now(void)
{
    uint64_t t = 0;
    if (rb_plat && rb_plat->now_ms)
        t = rb_plat->now_ms();
    if (!g_log_origin_set) {
        g_log_origin = t;
        g_log_origin_set = 1;
    }
    if (t < g_log_origin)
        return 0;
    t -= g_log_origin;
    if (t > 0xffffffffull)
        t = 0xffffffffull;
    return (uint32_t)t;
}

static void fmt_log_time(char *buf, size_t cap, uint32_t ms, int with_s)
{
    unsigned sec = ms / 1000u;
    unsigned cs = (ms % 1000u) / 10u;
    char frac[3];
    if (!buf || cap < 4) return;
    frac[0] = (char)('0' + (cs / 10u) % 10u);
    frac[1] = (char)('0' + (cs % 10u));
    frac[2] = 0;
    if (with_s)
        snprintf(buf, cap, "%u.%ss", sec, frac);
    else
        snprintf(buf, cap, "%u.%s", sec, frac);
}

static void bootlog_append_ss(rb_session *ss, const char *line, uint32_t ms, uint8_t kind)
{
    if (!ss || !line) return;
    if (ss->boot_log_n >= RB_BOOTLOG_LINES) {
        int i;
        for (i = 1; i < RB_BOOTLOG_LINES; i++) {
            rb_strcpy(ss->boot_log[i - 1], sizeof(ss->boot_log[0]), ss->boot_log[i]);
            ss->boot_log_ms[i - 1] = ss->boot_log_ms[i];
            ss->boot_log_kind[i - 1] = ss->boot_log_kind[i];
        }
        ss->boot_log_n = RB_BOOTLOG_LINES - 1;
    }
    rb_strcpy(ss->boot_log[ss->boot_log_n], sizeof(ss->boot_log[0]), line);
    ss->boot_log_ms[ss->boot_log_n] = ms;
    ss->boot_log_kind[ss->boot_log_n] = kind;
    ss->boot_log_n++;
    ss->boot_log_seq++;
}

static void bootlog_drain_early(rb_session *ss)
{
    int i;
    if (!ss || g_early_n < 1) return;
    for (i = 0; i < g_early_n; i++)
        bootlog_append_ss(ss, g_early_log[i], g_early_ms[i], g_early_kind[i]);
    g_early_n = 0;
}

void rb_bootlog(rb_session *ss, const char *line)
{
    uint32_t ms;
    uint8_t kind;
    if (!line) return;
    if (!ss) ss = g_bootlog_ss;
    else g_bootlog_ss = ss;
    ms = bootlog_now();
    kind = bootlog_kind(line);
    if (!ss) {
        if (g_early_n >= 96) {
            int i;
            for (i = 1; i < 96; i++) {
                rb_strcpy(g_early_log[i - 1], sizeof(g_early_log[0]), g_early_log[i]);
                g_early_ms[i - 1] = g_early_ms[i];
                g_early_kind[i - 1] = g_early_kind[i];
            }
            g_early_n = 95;
        }
        rb_strcpy(g_early_log[g_early_n], sizeof(g_early_log[0]), line);
        g_early_ms[g_early_n] = ms;
        g_early_kind[g_early_n] = kind;
        g_early_n++;
        return;
    }
    bootlog_drain_early(ss);
    bootlog_append_ss(ss, line, ms, kind);
}

void rb_bootlogf(rb_session *ss, const char *fmt, ...)
{
    char line[RB_BOOTLOG_COLS];
    va_list ap;
    if (!fmt) return;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    rb_bootlog(ss, line);
}

void rb_debug_term_draw(rb_session *ss)
{
    float x, y, w, h, text_y, line_h, time_w;
    int i, vis, start, keep_full;
    const rb_font *cap;
    char elapsed[24];
    uint32_t now_ms = 0;
    if (!ss || !ui_debug_on(ss)) return;
    g_bootlog_ss = ss;
    keep_full = (ss->fb.damage_w <= 0 || ss->fb.damage_h <= 0);
    rb_debug_term_geom(ss, &x, &y, &w, &h);
    cap = ui_caption(ss);
    line_h = ui_compact(ss) ? 13.f : 14.f;
    time_w = ui_compact(ss) ? 44.f : 56.f;
    rb_gfx_round_rect(&ss->fb, x, y, w, h, 18.f, rb_token(RB_TOKEN_BG_SECONDARY, 255));
    rb_gfx_round_rect_stroke(&ss->fb, x, y, w, h, 18.f, 1.f, rb_token(RB_TOKEN_BORDER_DEFAULT, 255));
    rb_gfx_text(&ss->fb, cap, x + 24.f, y + 16.f, "Logs", rb_token(RB_TOKEN_TEXT_TERTIARY, 255));
    now_ms = bootlog_now();
    fmt_log_time(elapsed, sizeof(elapsed), now_ms, 1);
    {
        int tw = rb_gfx_text_width(cap, elapsed);
        rb_gfx_text(&ss->fb, cap, x + w - 24.f - (float)tw, y + 16.f, elapsed,
                    rb_token(RB_TOKEN_TEXT_TERTIARY, 255));
    }
    rb_gfx_fill_rect(&ss->fb, x + 24.f, y + 38.f, w - 48.f, 1.f, rb_token(RB_TOKEN_BORDER_SUBTLE, 255));
    vis = (int)((h - 52.f) / line_h);
    if (vis < 1) vis = 1;
    start = ss->boot_log_n - vis;
    if (start < 0) start = 0;
    text_y = y + 48.f;
    for (i = start; i < ss->boot_log_n; i++) {
        char tbuf[16];
        uint32_t ms = ss->boot_log_ms[i];
        uint32_t ink = rb_token(RB_TOKEN_TEXT_SECONDARY, 255);
        uint32_t dot = rb_token(RB_TOKEN_BORDER_STRONG, 255);
        if (ss->boot_log_kind[i] == 1) {
            ink = rb_token(RB_TOKEN_TEXT_PRIMARY, 255);
            dot = rb_token(RB_TOKEN_BRAND_PRIMARY, 255);
        } else if (ss->boot_log_kind[i] == 2) {
            ink = rb_token(RB_TOKEN_WARNING, 255);
            dot = rb_token(RB_TOKEN_WARNING, 255);
        }
        rb_gfx_circle(&ss->fb, x + 28.f, text_y + 6.f, 2.5f, dot);
        fmt_log_time(tbuf, sizeof(tbuf), ms, 0);
        rb_gfx_text(&ss->fb, cap, x + 36.f, text_y, tbuf, rb_token(RB_TOKEN_TEXT_TERTIARY, 255));
        rb_gfx_text(&ss->fb, cap, x + 36.f + time_w, text_y, ss->boot_log[i], ink);
        text_y += line_h;
    }
    ss->boot_log_shown = ss->boot_log_seq;
    if (keep_full) {
        rb_fb_damage_full(&ss->fb);
        return;
    }
    ss->fb.damage_x = (int)x - 2;
    ss->fb.damage_y = (int)y - 2;
    ss->fb.damage_w = (int)w + 4;
    ss->fb.damage_h = (int)h + 4;
}

void rb_debug_flush(rb_session *ss)
{
    if (!ss) ss = g_bootlog_ss;
    if (!ss || !ss->plat) return;
    if (!ui_debug_on(ss)) return;
    if (ss->boot_log_seq == ss->boot_log_shown && ss->splash_bg_ready)
        return;
    if ((ss->phase == RB_PHASE_LOAD || ss->phase == RB_PHASE_SPLASH) && !ss->splash_bg_ready)
        rb_splash_draw(ss, ss->load_visual > 0.f ? ss->load_visual : 1.f, 1.f);
    else
        rb_debug_term_draw(ss);
    if (ss->plat->present) ss->plat->present(&ss->fb);
}

void rb_verbose_draw(rb_session *ss)
{
    if (!ss) return;
    rb_gfx_clear(&ss->fb, rb_token(RB_TOKEN_BG_PRIMARY, 255));
    if (ui_debug_on(ss))
        rb_debug_term_draw(ss);
    rb_fb_damage_full(&ss->fb);
}

void rb_settings_draw(rb_session *ss)
{
    float pad, card_w, card_h, card_x, card_y, row_y, tog_w, tog_h, tog_x, tog_y, knob;
    int on;
    const rb_font *body, *cap, *title;
    if (!ss) return;
    pad = ui_compact(ss) ? 22.f : 40.f;
    on = rb_debug_enabled(&ss->env);
    body = ui_body(ss);
    cap = ui_caption(ss);
    title = ss->font_title.atlas ? &ss->font_title : body;
    card_w = ui_compact(ss) ? (float)ss->fb.w - pad * 2.f : 560.f;
    card_h = 160.f;
    card_x = ((float)ss->fb.w - card_w) * 0.5f;
    card_y = ((float)ss->fb.h - ui_debug_reserve(ss)) * 0.5f - card_h * 0.5f;
    if (card_y < pad + 72.f) card_y = pad + 72.f;
    tog_w = 51.f;
    tog_h = 31.f;
    rb_gfx_clear(&ss->fb, rb_token(RB_TOKEN_BG_PRIMARY, 255));
    rb_gfx_text_center(&ss->fb, title, (float)ss->fb.w * 0.5f, pad + 8.f, "Settings",
                       rb_token(RB_TOKEN_TEXT_PRIMARY, 255));
    rb_gfx_text_center(&ss->fb, cap, (float)ss->fb.w * 0.5f, pad + 48.f, "Startup",
                       rb_token(RB_TOKEN_TEXT_TERTIARY, 255));
    rb_gfx_round_rect(&ss->fb, card_x, card_y, card_w, card_h, 18.f,
                      rb_token(RB_TOKEN_BG_SECONDARY, 255));
    row_y = card_y + 36.f;
    rb_gfx_text(&ss->fb, body, card_x + 32.f, row_y, "Debug",
                rb_token(RB_TOKEN_TEXT_PRIMARY, 255));
    rb_gfx_text(&ss->fb, cap, card_x + 32.f, row_y + 28.f, "Show verbose logs while starting up",
                rb_token(RB_TOKEN_TEXT_TERTIARY, 255));
    tog_x = card_x + card_w - 32.f - tog_w;
    tog_y = row_y + 4.f;
    rb_gfx_round_rect(&ss->fb, tog_x, tog_y, tog_w, tog_h, tog_h * 0.5f,
                      on ? rb_token(RB_TOKEN_BRAND_PRIMARY, 255) : rb_token(RB_TOKEN_BG_TERTIARY, 255));
    knob = on ? tog_x + tog_w - tog_h + 2.f : tog_x + 2.f;
    rb_gfx_circle(&ss->fb, knob + (tog_h - 4.f) * 0.5f, tog_y + tog_h * 0.5f, (tog_h - 6.f) * 0.5f,
                  rb_token(RB_TOKEN_TEXT_ON_BRAND, 255));
    {
        float hint_y = (float)ss->fb.h - 48.f;
        if (on)
            hint_y = (float)ss->fb.h - ui_debug_reserve(ss) - 8.f;
        rb_gfx_text_center(&ss->fb, cap, (float)ss->fb.w * 0.5f, hint_y,
                          "Enter to toggle   Esc to return",
                          rb_token(RB_TOKEN_TEXT_TERTIARY, 255));
        if (on)
            rb_debug_term_draw(ss);
    }
    rb_fb_damage_full(&ss->fb);
}

void rb_klog_draw(rb_session *ss)
{
    char line[96];
    float pad, y;
    const rb_font *body, *cap, *title;
    if (!ss) return;
    if (ui_debug_on(ss)) {
        rb_gfx_clear(&ss->fb, rb_token(RB_TOKEN_BG_PRIMARY, 255));
        rb_debug_term_draw(ss);
        rb_fb_damage_full(&ss->fb);
        return;
    }
    pad = ui_compact(ss) ? 22.f : 40.f;
    body = ui_body(ss);
    cap = ui_caption(ss);
    title = ss->font_title.atlas ? &ss->font_title : body;
    rb_gfx_clear(&ss->fb, rb_token(RB_TOKEN_BG_PRIMARY, 255));
    rb_gfx_text(&ss->fb, title, pad, pad, "Roboticela Kernel", rb_token(RB_TOKEN_TEXT_PRIMARY, 255));
    rb_gfx_text(&ss->fb, cap, pad, pad + 36.f, "-----------------", rb_token(RB_TOKEN_TEXT_TERTIARY, 255));
    y = pad + 72.f;
    rb_gfx_text(&ss->fb, body, pad, y, "Boot protocol: Roboticela", rb_token(RB_TOKEN_TEXT_PRIMARY, 255));
    y += 22.f;
    rb_gfx_text(&ss->fb, body, pad, y, "CPU: x86_64", rb_token(RB_TOKEN_TEXT_PRIMARY, 255));
    y += 36.f;
    rb_gfx_text(&ss->fb, cap, pad, y, "Memory:", rb_token(RB_TOKEN_TEXT_TERTIARY, 255));
    y += 22.f;
    rb_gfx_text(&ss->fb, body, pad, y, "  Available: host", rb_token(RB_TOKEN_TEXT_PRIMARY, 255));
    y += 22.f;
    rb_gfx_text(&ss->fb, body, pad, y, "  Reserved:  host", rb_token(RB_TOKEN_TEXT_PRIMARY, 255));
    y += 36.f;
    rb_gfx_text(&ss->fb, cap, pad, y, "Framebuffer:", rb_token(RB_TOKEN_TEXT_TERTIARY, 255));
    y += 22.f;
    snprintf(line, sizeof(line), "  %dx%d", ss->fb.w, ss->fb.h);
    rb_gfx_text(&ss->fb, body, pad, y, line, rb_token(RB_TOKEN_TEXT_PRIMARY, 255));
    y += 22.f;
    rb_gfx_text(&ss->fb, body, pad, y, "  32 BPP", rb_token(RB_TOKEN_TEXT_PRIMARY, 255));
    y += 36.f;
    rb_gfx_text(&ss->fb, cap, pad, y, "ACPI:", rb_token(RB_TOKEN_TEXT_TERTIARY, 255));
    y += 22.f;
    rb_gfx_text(&ss->fb, body, pad, y, "  RSDP found", rb_token(RB_TOKEN_TEXT_PRIMARY, 255));
    y += 36.f;
    rb_gfx_text(&ss->fb, cap, pad, y, "Kernel:", rb_token(RB_TOKEN_TEXT_TERTIARY, 255));
    y += 22.f;
    snprintf(line, sizeof(line), "  address: 0x%llx", (unsigned long long)ss->handoff.kernel.entry);
    rb_gfx_text(&ss->fb, body, pad, y, line, rb_token(RB_TOKEN_TEXT_PRIMARY, 255));
    y += 22.f;
    snprintf(line, sizeof(line), "  size: %zu", ss->handoff.kernel.valid ? ss->handoff.kernel.size : 0);
    rb_gfx_text(&ss->fb, body, pad, y, line, rb_token(RB_TOKEN_TEXT_PRIMARY, 255));
    y += 36.f;
    rb_gfx_text(&ss->fb, cap, pad, y, "InitFS:", rb_token(RB_TOKEN_TEXT_TERTIARY, 255));
    y += 22.f;
    snprintf(line, sizeof(line), "  address: 0x%llx", (unsigned long long)ss->handoff.ramdisk.phys);
    rb_gfx_text(&ss->fb, body, pad, y, line, rb_token(RB_TOKEN_TEXT_PRIMARY, 255));
    y += 22.f;
    snprintf(line, sizeof(line), "  size: %zu", ss->handoff.ramdisk.valid ? ss->handoff.ramdisk.size : 0);
    rb_gfx_text(&ss->fb, body, pad, y, line, rb_token(RB_TOKEN_TEXT_PRIMARY, 255));
    ss->boot_log_shown = ss->boot_log_seq;
}
