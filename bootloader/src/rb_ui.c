#include "roboboot.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

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
    return ui_compact(ss) ? 96.f : 160.f;
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

void rb_splash_draw(rb_session *ss, float visual, float fade)
{
    float cx, logo_h, lw, lh, lx, ly, bar_w, bar_h, bar_x, bar_y, fill, stack, top;
    int bx, by, bw, bh, fw;
    uint32_t bg, track, brand;
    if (!ss) return;
    (void)fade;
    visual = rb_clampf(visual, 0.f, 1.f);
    cx = ss->fb.w * 0.5f;
    logo_h = ui_compact(ss) ? 96.f : 160.f;
    lh = logo_h;
    lw = lh;
    if (ss->logo.px && ss->logo.h > 0)
        lw = lh * ((float)ss->logo.w / (float)ss->logo.h);
    bar_w = 176.f;
    bar_h = 4.f;
    stack = lh + 48.f + bar_h;
    top = (float)ss->fb.h * 0.5f - stack * 0.5f;
    lx = cx - lw * 0.5f;
    ly = top;
    bar_x = cx - bar_w * 0.5f;
    bar_y = ly + lh + 48.f;
    bx = (int)bar_x;
    by = (int)bar_y;
    bw = (int)bar_w;
    bh = (int)bar_h;
    if (bh < 4) bh = 4;
    fill = bar_w * visual;
    fw = (int)(fill + 0.5f);
    if (fw < 0) fw = 0;
    if (fw > bw) fw = bw;
    bg = rb_token(RB_TOKEN_BG_PRIMARY, 255);
    track = rb_token(RB_TOKEN_BORDER_DEFAULT, 255);
    brand = rb_token(RB_TOKEN_BRAND_PRIMARY, 255);

    if (!ss->splash_bg_ready) {
        rb_gfx_clear(&ss->fb, bg);
        if (ss->logo.px)
            rb_gfx_blit(&ss->fb, &ss->logo, lx, ly, lw, lh, 1.f);
        else
            rb_gfx_round_rect(&ss->fb, lx, ly, lw, lh, 18.f, brand);
        rb_gfx_fill_box(&ss->fb, bx, by, bw, bh, track);
        if (fw > 0)
            rb_gfx_fill_box(&ss->fb, bx, by, fw, bh, brand);
        ss->splash_bg_ready = 1;
        ss->fb.damage_w = 0;
        ss->fb.damage_h = 0;
        return;
    }

    rb_gfx_fill_box(&ss->fb, bx, by, bw, bh, track);
    if (fw > 0)
        rb_gfx_fill_box(&ss->fb, bx, by, fw, bh, brand);
    ss->fb.damage_x = bx - 2;
    ss->fb.damage_y = by - 2;
    ss->fb.damage_w = bw + 4;
    ss->fb.damage_h = bh + 4;
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
    *oy = ss->fb.h * 0.5f - 24.f;
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

    rb_gfx_text_center(&ss->fb, ui_caption(ss), ss->fb.w * 0.5f, (float)ss->fb.h - 48.f,
                      ss->catalog.picker_hint[0] ? ss->catalog.picker_hint : "Choose a startup volume",
                      rb_token(RB_TOKEN_TEXT_TERTIARY, 255));
    rb_gfx_text(&ss->fb, ui_caption(ss), ui_edge(ss), (float)ss->fb.h - 48.f, "Settings",
                rb_token(RB_TOKEN_TEXT_TERTIARY, 255));

    if (ss->catalog.picker_timeout_s > 0 && ss->timeout_left_ms > 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Booting in %d", (ss->timeout_left_ms + 999) / 1000);
        rb_gfx_text_center(&ss->fb, ui_caption(ss), ss->fb.w * 0.5f, (float)ss->fb.h - 64.f, buf,
                           rb_token(RB_TOKEN_TEXT_TERTIARY, 255));
    }
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

void rb_bootlog(rb_session *ss, const char *line)
{
    if (!ss || !line) return;
    if (ss->boot_log_n >= 24) {
        int i;
        for (i = 1; i < 24; i++)
            rb_strcpy(ss->boot_log[i - 1], sizeof(ss->boot_log[0]), ss->boot_log[i]);
        ss->boot_log_n = 23;
    }
    rb_strcpy(ss->boot_log[ss->boot_log_n++], sizeof(ss->boot_log[0]), line);
    rb_logf("%s", line);
}

static const rb_font *ui_body(const rb_session *ss)
{
    return ss->font_body.atlas ? &ss->font_body : ui_caption(ss);
}

void rb_verbose_draw(rb_session *ss)
{
    float pad, y;
    int i;
    const rb_font *body, *cap, *title;
    if (!ss) return;
    pad = ui_compact(ss) ? 22.f : 40.f;
    body = ui_body(ss);
    cap = ui_caption(ss);
    title = ss->font_title.atlas ? &ss->font_title : body;
    rb_gfx_clear(&ss->fb, rb_token(RB_TOKEN_BG_PRIMARY, 255));
    rb_gfx_text(&ss->fb, title, pad, pad, "RoboBoot", rb_token(RB_TOKEN_TEXT_PRIMARY, 255));
    rb_gfx_text(&ss->fb, cap, pad, pad + 36.f, "Startup", rb_token(RB_TOKEN_TEXT_TERTIARY, 255));
    rb_gfx_fill_rect(&ss->fb, pad, pad + 64.f, (float)ss->fb.w - pad * 2.f, 1.f,
                     rb_token(RB_TOKEN_BORDER_SUBTLE, 255));
    y = pad + 88.f;
    for (i = 0; i < ss->boot_log_n; i++) {
        rb_gfx_text(&ss->fb, body, pad, y, ss->boot_log[i],
                    rb_token(RB_TOKEN_TEXT_SECONDARY, 255));
        y += 22.f;
        if (y > (float)ss->fb.h - 48.f) break;
    }
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
    card_y = (float)ss->fb.h * 0.5f - card_h * 0.5f;
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
    rb_gfx_text_center(&ss->fb, cap, (float)ss->fb.w * 0.5f, (float)ss->fb.h - 48.f,
                      "Enter to toggle   Esc to return",
                      rb_token(RB_TOKEN_TEXT_TERTIARY, 255));
}

void rb_klog_draw(rb_session *ss)
{
    char line[96];
    float pad, y;
    const rb_font *body, *cap, *title;
    if (!ss) return;
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
}
