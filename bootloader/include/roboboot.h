#ifndef ROBOBOOT_H
#define ROBOBOOT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define RB_VERSION_STR "1.0.0"
#define RB_MAX_TARGETS 32
#define RB_MAX_PATH 512
#define RB_MAX_TITLE 96
#define RB_MAX_ARGS 512
#define RB_MAX_ID 48
#define RB_MAX_MODULES 8
#define RB_MAX_ENV 64
#define RB_MAX_LINE 1024
#define RB_ENV_BLOCK 4096
#define RB_SPLASH_MIN_MS 2000
#define RB_SPLASH_HANDOFF 0.40f
#define RB_BOOTLOG_COLS 1024

typedef struct {
    char *text;
    uint32_t ms;
    uint8_t kind;
} rb_log_line;

/* Roboticela palette, dark mode. Boot is one dark view — do not mix light-mode hex. */
#define RB_TOKEN_BRAND_PRIMARY          0x3B9EFFu
#define RB_TOKEN_BRAND_PRIMARY_HOVER    0x5CADFFu
#define RB_TOKEN_BRAND_PRIMARY_ACTIVE   0x2E8CE6u
#define RB_TOKEN_TEXT_PRIMARY           0xFFFFFFu
#define RB_TOKEN_TEXT_SECONDARY         0xA1A1A6u
#define RB_TOKEN_TEXT_TERTIARY          0x98989Du
#define RB_TOKEN_TEXT_DISABLED          0x48484Au
#define RB_TOKEN_TEXT_ON_BRAND          0xFFFFFFu
#define RB_TOKEN_BG_PRIMARY             0x000000u
#define RB_TOKEN_BG_SECONDARY           0x1C1C1Eu
#define RB_TOKEN_BG_TERTIARY            0x2C2C2Eu
#define RB_TOKEN_BG_ELEVATED            0x2C2C2Eu
#define RB_TOKEN_BG_HOVER               0x2C2C2Eu
#define RB_TOKEN_BG_ACTIVE              0x3A3A3Cu
#define RB_TOKEN_BORDER_DEFAULT         0x38383Au
#define RB_TOKEN_BORDER_SUBTLE          0x2C2C2Eu
#define RB_TOKEN_BORDER_STRONG          0x48484Au
#define RB_TOKEN_BORDER_FOCUS           0x3B9EFFu
#define RB_TOKEN_DANGER                 0xFF453Au
#define RB_TOKEN_SUCCESS                0x30D158u
#define RB_TOKEN_WARNING                0xFF9F0Au
#define RB_TOKEN_ACCENT_TEAL            0x2DD4C4u
#define RB_TOKEN_BRAND_SUBTLE_A         0.15f
#define RB_TOKEN_BRAND_FOCUS_A          0.30f

typedef enum {
    RB_OK = 0,
    RB_ERR = -1,
    RB_ERR_NOMEM = -2,
    RB_ERR_IO = -3,
    RB_ERR_PARSE = -4,
    RB_ERR_NOTFOUND = -5,
    RB_ERR_AUTH = -6,
    RB_ERR_FORMAT = -7,
    RB_ERR_UNSUPPORTED = -8,
    RB_ERR_CANCEL = -9,
    RB_ERR_FAIL = -10
} rb_status;

typedef enum {
    RB_KIND_KERNEL = 0,
    RB_KIND_LINUX,
    RB_KIND_MULTIBOOT2,
    RB_KIND_EFI_CHAIN,
    RB_KIND_FIRMWARE,
    RB_KIND_REBOOT,
    RB_KIND_HALT
} rb_kind;

typedef enum {
    RB_KEY_NONE = 0,
    RB_KEY_LEFT,
    RB_KEY_RIGHT,
    RB_KEY_UP,
    RB_KEY_DOWN,
    RB_KEY_ENTER,
    RB_KEY_ESC,
    RB_KEY_BACKSPACE,
    RB_KEY_TAB,
    RB_KEY_F10,
    RB_KEY_F8,
    RB_KEY_CONSOLE,
    RB_KEY_CHAR
} rb_key;

enum {
    RB_MOD_ALT = 1u << 0,
    RB_MOD_SHIFT = 1u << 1,
    RB_MOD_CTRL = 1u << 2
};

typedef struct {
    uint32_t mods;
    int keys[24];
    int nkeys;
    int chars[8];
    int nchars;
    int quit;
} rb_input;

typedef struct {
    int w;
    int h;
    uint32_t *px;
    int damage_x;
    int damage_y;
    int damage_w;
    int damage_h;
} rb_surface;

typedef struct {
    int w;
    int h;
    uint32_t *px;
} rb_image;

typedef struct {
    uint16_t x, y;
    int16_t w, h;
    uint16_t advance;
    int16_t offx, offy;
} rb_glyph;

typedef struct {
    int px;
    int first, last, count;
    int atlas_w, atlas_h;
    rb_glyph glyphs[128];
    uint8_t *atlas;
} rb_font;

typedef struct {
    char id[RB_MAX_ID];
    char title[RB_MAX_TITLE];
    rb_kind kind;
    char icon[RB_MAX_PATH];
    char volume[RB_MAX_PATH];
    char kernel[RB_MAX_PATH];
    char ramdisk[RB_MAX_PATH];
    char chain[RB_MAX_PATH];
    char args[RB_MAX_ARGS];
    char modules[RB_MAX_MODULES][RB_MAX_PATH];
    int nmodules;
    int hidden;
    int restricted;
    char lock[192];
    int hotkey;
    int present;
} rb_target;

typedef struct {
    int version;
    int splash_min_ms;
    char splash_logo[RB_MAX_PATH];
    uint32_t picker_key;
    int picker_timeout_s;
    char picker_hint[RB_MAX_TITLE];
    char default_id[RB_MAX_ID];
    char fallback_id[RB_MAX_ID];
    int save_last;
    char env_path[RB_MAX_PATH];
    rb_target targets[RB_MAX_TARGETS];
    int ntargets;
} rb_catalog;

typedef struct {
    char keys[RB_MAX_ENV][64];
    char vals[RB_MAX_ENV][192];
    int n;
    char path[RB_MAX_PATH];
} rb_env;

typedef struct {
    char name[64];
    char path[RB_MAX_PATH];
    char fs[16];
    int has_kernel;
    int has_efi;
} rb_volume;

typedef struct {
    rb_volume vols[16];
    int nvols;
} rb_probe;

typedef struct {
    uint8_t *bytes;
    size_t size;
    uint64_t entry;
    uint64_t phys;
    int valid;
} rb_image_blob;

typedef struct {
    rb_image_blob kernel;
    rb_image_blob ramdisk;
    rb_image_blob extras[RB_MAX_MODULES];
    int nextras;
    char cmdline[RB_MAX_ARGS];
    rb_kind protocol;
    int debug;
    int debug_term_x, debug_term_y, debug_term_w, debug_term_h;
    int splash_bar_x, splash_bar_y, splash_bar_w, splash_bar_h;
    uint32_t splash_progress;
} rb_handoff;

typedef struct rb_platform {
    uint64_t (*now_ms)(void);
    void (*sleep_ms)(uint32_t ms);
    void (*poll_input)(rb_input *out);
    int (*present)(rb_surface *s);
    rb_status (*read_file)(const char *path, void **data, size_t *len);
    rb_status (*write_file)(const char *path, const void *data, size_t len);
    rb_status (*list_dir)(const char *path, void (*cb)(const char *name, int is_dir, void *user), void *user);
    void (*log)(const char *msg);
    rb_status (*boot)(const rb_target *t, const rb_handoff *h);
    rb_status (*firmware_setup)(void);
    rb_status (*reboot)(void);
    rb_status (*halt)(void);
    const char *root;
} rb_platform;

typedef enum {
    RB_PHASE_LOAD = 0,
    RB_PHASE_SPLASH,
    RB_PHASE_PICKER,
    RB_PHASE_AUTH,
    RB_PHASE_CONSOLE,
    RB_PHASE_SETTINGS,
    RB_PHASE_VERBOSE,
    RB_PHASE_KLOG,
    RB_PHASE_HANDOFF,
    RB_PHASE_DONE
} rb_phase;

typedef struct {
    const rb_platform *plat;
    rb_catalog catalog;
    rb_env env;
    rb_probe probe;
    rb_surface fb;
    rb_image logo;
    rb_image icons[RB_MAX_TARGETS];
    rb_font font_title;
    rb_font font_body;
    rb_font font_caption;
    rb_input input;
    rb_phase phase;
    int force_menu;
    int windowed;
    float load_real;
    float load_visual;
    uint64_t splash_start_ms;
    int splash_done;
    int splash_inited;
    int splash_bg_ready;
    int splash_bar_x, splash_bar_y, splash_bar_w, splash_bar_h;
    int splash_ticks;
    int logo_fitted;
    int icons_loaded;
    int icons_fitted;
    int picker_drawn;
    int settings_drawn;
    int alt_latched;
    int selected;
    float sel_anim;
    int timeout_left_ms;
    uint64_t last_ms;
    char auth_buf[64];
    int auth_len;
    int auth_fail;
    char console_in[256];
    int console_len;
    char console_log[16][240];
    int console_lines;
    int running;
    int boot_index;
    rb_handoff handoff;
    float fade;
    rb_log_line *boot_log;
    int boot_log_n;
    int boot_log_cap;
    int boot_log_seq;
    int boot_log_shown;
    int boot_log_follow;
    int boot_log_top;
} rb_session;

extern const rb_platform *rb_plat;

void *rb_alloc(size_t n);
void *rb_calloc(size_t n);
void rb_free(void *p);
void rb_zero(void *p, size_t n);
void rb_copy(void *dst, const void *src, size_t n);
int rb_strlen(const char *s);
int rb_strcmp(const char *a, const char *b);
int rb_strncmp(const char *a, const char *b, size_t n);
void rb_strcpy(char *dst, size_t cap, const char *src);
int rb_str_eq(const char *a, const char *b);
int rb_endswith(const char *s, const char *suf);
void rb_join(char *dst, size_t cap, const char *a, const char *b);
void rb_logf(const char *fmt, ...);
uint32_t rb_rgba(int r, int g, int b, int a);
uint32_t rb_token(uint32_t rgb, int alpha);
uint32_t rb_tokenf(uint32_t rgb, float fade);
uint32_t rb_blend(uint32_t dst, uint32_t src);
float rb_clampf(float v, float lo, float hi);
float rb_lerpf(float a, float b, float t);
float rb_smoothstep(float t);
float rb_ease_apple(float t);

rb_status rb_sha256(const void *data, size_t len, uint8_t out[32]);
void rb_hex(const uint8_t *in, size_t n, char *out, size_t cap);
int rb_unhex(const char *in, uint8_t *out, size_t cap);
rb_status rb_lock_hash(const char *password, const uint8_t *salt, size_t salt_len, int rounds, uint8_t out[32]);
int rb_lock_verify(const char *spec, const char *password);

rb_status rb_image_load_mem(const void *data, size_t len, rb_image *out);
rb_status rb_image_resize(const rb_image *src, int w, int h, rb_image *dst);
void rb_image_free(rb_image *im);
rb_status rb_font_load_mem(const void *data, size_t len, rb_font *out);
void rb_font_free(rb_font *f);

void rb_gfx_clear(rb_surface *s, uint32_t c);
void rb_fb_damage_full(rb_surface *s);
void rb_gfx_dot(rb_surface *s, int x, int y, uint32_t c);
void rb_gfx_fill_box(rb_surface *s, int x, int y, int w, int h, uint32_t c);
void rb_gfx_fill_rect(rb_surface *s, float x, float y, float w, float h, uint32_t c);
void rb_gfx_round_rect(rb_surface *s, float x, float y, float w, float h, float r, uint32_t c);
void rb_gfx_round_rect_stroke(rb_surface *s, float x, float y, float w, float h, float r, float t, uint32_t c);
void rb_gfx_circle(rb_surface *s, float cx, float cy, float r, uint32_t c);
void rb_gfx_ring(rb_surface *s, float cx, float cy, float r, float t, uint32_t c);
void rb_gfx_blit(rb_surface *s, const rb_image *im, float x, float y, float w, float h, float alpha);
int rb_gfx_text_width(const rb_font *f, const char *text);
void rb_gfx_text(rb_surface *s, const rb_font *f, float x, float y, const char *text, uint32_t c);
void rb_gfx_text_center(rb_surface *s, const rb_font *f, float cx, float y, const char *text, uint32_t c);

rb_status rb_manifest_parse(const char *text, rb_catalog *out);
rb_status rb_manifest_load_path(const char *path, rb_catalog *out);
int rb_catalog_find(const rb_catalog *c, const char *id);
rb_kind rb_kind_from_name(const char *s);
const char *rb_kind_name(rb_kind k);

rb_status rb_env_parse(const char *text, rb_env *out);
rb_status rb_env_load(const char *path, rb_env *out);
rb_status rb_env_save(const rb_env *env);
const char *rb_env_get(const rb_env *env, const char *key);
void rb_env_set(rb_env *env, const char *key, const char *val);

rb_status rb_probe_volumes(const char *volumes_root, rb_probe *out);
void rb_probe_apply(rb_catalog *c, const rb_probe *p);

float rb_splash_visual(float real_progress, uint64_t elapsed_ms, int min_ms);
int rb_splash_complete(float real_progress, uint64_t elapsed_ms, int min_ms);

typedef enum {
    RB_BOOT_DEFAULT = 0,
    RB_BOOT_PICKER,
    RB_BOOT_ONCE
} rb_boot_intent;

rb_boot_intent rb_policy_intent(int alt_held, int force_menu, const char *next_boot);
int rb_policy_resolve_index(const rb_catalog *c, const rb_env *env, int fallback);
int rb_policy_next_index(const rb_catalog *c, int from, int dir);

#define RB_MAX_PHDRS 16

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} rb_phdr;

int rb_debug_enabled(const rb_env *env);
void rb_debug_set(rb_env *env, int on);

rb_status rb_loader_prepare(const rb_target *t, const char *root, rb_handoff *h);
void rb_handoff_free(rb_handoff *h);
rb_status rb_elf64_info(const uint8_t *data, size_t len, uint64_t *entry, uint64_t *phoff, int *phnum);
int rb_elf64_phdrs(const uint8_t *data, size_t len, rb_phdr *out, int cap);

void rb_splash_draw(rb_session *ss, float visual, float fade);
void rb_picker_draw(rb_session *ss);
void rb_auth_draw(rb_session *ss);
void rb_settings_draw(rb_session *ss);
void rb_verbose_draw(rb_session *ss);
void rb_klog_draw(rb_session *ss);
void rb_bootlog(rb_session *ss, const char *line);
void rb_bootlogf(rb_session *ss, const char *fmt, ...);
void rb_bootlog_freeze(void);
void rb_bootlog_release(rb_session *ss);
int rb_bootlog_handoff_begin(void *buf, uint32_t size);
void rb_bootlog_handoff_info(uint32_t *used, uint32_t *count);
void rb_debug_term_geom(const rb_session *ss, float *x, float *y, float *w, float *h);
void rb_debug_term_draw(rb_session *ss);
void rb_debug_flush(rb_session *ss);
int rb_debug_log_scroll(rb_session *ss, int delta);
void rb_console_draw(rb_session *ss);
void rb_console_submit(rb_session *ss);
void rb_console_print(rb_session *ss, const char *line);

void rb_session_init(rb_session *ss, const rb_platform *plat);
void rb_session_free(rb_session *ss);
rb_status rb_session_load(rb_session *ss);
rb_status rb_session_boot(rb_session *ss);
int rb_session_tick(rb_session *ss);

#endif
