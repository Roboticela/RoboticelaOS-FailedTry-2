#include "roboboot.h"
#include "roboticela/bootinfo.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void FmtBytes(char *out, size_t cap, size_t n)
{
    if (!out || cap < 1) return;
    if (n >= 1024u * 1024u)
        snprintf(out, cap, "%zu.%zu MB", n / (1024u * 1024u), (n / 102400u) % 10u);
    else if (n >= 1024u)
        snprintf(out, cap, "%zu KB", n / 1024u);
    else
        snprintf(out, cap, "%zu bytes", n);
}

static RoboRbStatus LoadPathImage(Session *ss, const char *rel, Image *im)
{
    char path[RbMaxPath];
    char bytes[32];
    void *data = NULL;
    size_t len = 0;
    RoboRbStatus st;
    ImageFree(im);
    Join(path, sizeof(path), ss->plat->root, rel);
    Bootlogf(ss, "Read  %s", rel);
    st = ss->plat->RoboReadFile(path, &data, &len);
    if (st != RbOk) {
        Bootlogf(ss, "Read  failed  %s  (%d)", rel, st);
        return st;
    }
    FmtBytes(bytes, sizeof(bytes), len);
    Bootlogf(ss, "Read  %s  %s", rel, bytes);
    st = ImageLoadMem(data, len, im);
    RoboRbFree(data);
    if (st != RbOk) {
        Bootlogf(ss, "RBIM  decode failed  %s  (%d)", rel, st);
        return st;
    }
    Bootlogf(ss, "RBIM  %s  %d x %d", rel, im->w, im->h);
    return st;
}

static RoboRbStatus LoadPathFont(Session *ss, const char *rel, Font *font)
{
    char path[RbMaxPath];
    char bytes[32];
    void *data = NULL;
    size_t len = 0;
    RoboRbStatus st;
    FontFree(font);
    Join(path, sizeof(path), ss->plat->root, rel);
    Bootlogf(ss, "Read  %s", rel);
    st = ss->plat->RoboReadFile(path, &data, &len);
    if (st != RbOk) {
        Bootlogf(ss, "Font  missing  %s  (%d)", rel, st);
        return st;
    }
    FmtBytes(bytes, sizeof(bytes), len);
    Bootlogf(ss, "Read  %s  %s", rel, bytes);
    st = FontLoadMem(data, len, font);
    RoboRbFree(data);
    if (st != RbOk) {
        Bootlogf(ss, "Font  decode failed  %s  (%d)", rel, st);
        return st;
    }
    Bootlogf(ss, "Font  %s  %d px  glyphs %d-%d  atlas %dx%d",
                rel, font->px, font->first, font->last, font->atlasW, font->atlasH);
    return st;
}

static float BumpLoad(Session *ss, float to)
{
    if (to > ss->loadReal) ss->loadReal = to;
    return ss->loadReal;
}

static RoboRbStatus LoadResources(Session *ss)
{
    char path[RbMaxPath];
    RoboRbStatus st;
    int i, present = 0;

    BumpLoad(ss, 0.04f);
    Bootlogf(ss, "RoboBoot %s", RbVersionStr);
    Bootlog(ss, "Load  catalog");
    Join(path, sizeof(path), ss->plat->root, "config/roboboot.manifest");
    Bootlogf(ss, "Open  %s", path[0] ? path : "config/roboboot.manifest");
    st = ManifestLoadPath(path, &ss->catalog);
    if (st != RbOk) {
        Bootlogf(ss, "Catalog  parse failed  (%d)", st);
        return st;
    }
    Bootlogf(ss, "Catalog  v%d  %d targets", ss->catalog.version, ss->catalog.ntargets);
    Bootlogf(ss, "Catalog  splash %d ms  timeout %d s",
                ss->catalog.splashMinMs, ss->catalog.pickerTimeoutS);
    Bootlogf(ss, "Catalog  default %s  fallback %s",
                ss->catalog.defaultId[0] ? ss->catalog.defaultId : "-",
                ss->catalog.fallbackId[0] ? ss->catalog.fallbackId : "-");
    if (ss->catalog.pickerHint[0])
        Bootlogf(ss, "Catalog  hint  %s", ss->catalog.pickerHint);
    BumpLoad(ss, 0.16f);

    Bootlog(ss, "Load  settings");
    Join(path, sizeof(path), ss->plat->root, ss->catalog.envPath);
    st = EnvLoad(path, &ss->env);
    Bootlogf(ss, "Settings  %s  (%d)", ss->catalog.envPath, st);
    {
        int k;
        const char *next = EnvGet(&ss->env, "next_boot");
        const char *last = EnvGet(&ss->env, "last_boot");
        Bootlogf(ss, "Settings  %d keys  debug %s",
                    ss->env.n, RoboRbDebugEnabled(&ss->env) ? "on" : "off");
        if (RoboRbDebugEnabled(&ss->env))
            Bootlog(ss, "Logs  Up/Down to scroll");
        for (k = 0; k < ss->env.n; k++) {
            if (ss->env.keys[k][0] == 0) continue;
            Bootlogf(ss, "Env  %s=%s", ss->env.keys[k], ss->env.vals[k]);
        }
        Bootlogf(ss, "Env  last %s  next %s",
                    last && last[0] ? last : "-",
                    next && next[0] ? next : "-");
    }
    BumpLoad(ss, 0.26f);

    Bootlog(ss, "Probe  volumes/");
    Join(path, sizeof(path), ss->plat->root, "volumes");
    ProbeVolumes(path, &ss->probe);
    for (i = 0; i < ss->probe.nvols; i++) {
        Bootlogf(ss, "Volume  %s  kernel %s  efi %s",
                    ss->probe.vols[i].name,
                    ss->probe.vols[i].hasKernel ? "yes" : "no",
                    ss->probe.vols[i].hasEfi ? "yes" : "no");
    }
    ProbeApply(&ss->catalog, &ss->probe);
    for (i = 0; i < ss->catalog.ntargets; i++) {
        Target *t = &ss->catalog.targets[i];
        if (t->present) present++;
        Bootlogf(ss, "Target  %s  %s  vol %s  %s%s%s",
                    t->id, KindName(t->kind),
                    t->volume[0] ? t->volume : "-",
                    t->present ? "ready" : "missing",
                    t->hidden ? "  hidden" : "",
                    t->restricted ? "  locked" : "");
        if (t->kernel[0])
            Bootlogf(ss, "  kernel  %s", t->kernel);
        if (t->ramdisk[0])
            Bootlogf(ss, "  initfs  %s", t->ramdisk);
        if (t->chain[0])
            Bootlogf(ss, "  chain  %s", t->chain);
        if (t->icon[0])
            Bootlogf(ss, "  picture  %s", t->icon);
        if (t->args[0])
            Bootlogf(ss, "  args  %s", t->args);
        if (t->hotkey)
            Bootlogf(ss, "  hotkey  %c", t->hotkey);
    }
    Bootlogf(ss, "Probe  %d folders  %d ready", ss->probe.nvols, present);
    BumpLoad(ss, 0.46f);

    Bootlogf(ss, "Logo  %s", ss->catalog.splashLogo[0] ? ss->catalog.splashLogo : "(none)");
    st = LoadPathImage(ss, ss->catalog.splashLogo, &ss->logo);
    if (st != RbOk || !ss->logo.px)
        Bootlog(ss, "Logo  missing");
    BumpLoad(ss, 0.60f);

    Bootlog(ss, "Fonts");
    LoadPathFont(ss, "assets/fonts/ui-title.rbfn", &ss->fontTitle);
    LoadPathFont(ss, "assets/fonts/ui-body.rbfn", &ss->fontBody);
    LoadPathFont(ss, "assets/fonts/ui-caption.rbfn", &ss->fontCaption);
    BumpLoad(ss, 0.94f);

    ss->selected = PolicyResolveIndex(&ss->catalog, &ss->env, 0);
    if (ss->selected < 0) ss->selected = 0;
    Bootlogf(ss, "Policy  default index %d  %s",
                ss->selected, ss->catalog.targets[ss->selected].id);
    ss->timeoutLeftMs = ss->catalog.pickerTimeoutS * 1000;
    Bootlogf(ss, "Splash  hold %d ms  picker timeout %d s",
                ss->catalog.splashMinMs, ss->catalog.pickerTimeoutS);
    Bootlog(ss, "Resources ready");
    BumpLoad(ss, 1.f);
    return RbOk;
}

static void LoadPickerIcons(Session *ss)
{
    int i;
    if (!ss || ss->iconsLoaded) return;
    Bootlog(ss, "Picker  load pictures");
    for (i = 0; i < ss->catalog.ntargets; i++) {
        if (ss->catalog.targets[i].icon[0]) {
            if (LoadPathImage(ss, ss->catalog.targets[i].icon, &ss->icons[i]) != RbOk)
                Bootlogf(ss, "Picture  skip  %s", ss->catalog.targets[i].id);
        } else
            Bootlogf(ss, "Picture  none  %s", ss->catalog.targets[i].id);
    }
    ss->iconsLoaded = 1;
    Bootlog(ss, "Picker  pictures ready");
}

RoboRbStatus SessionLoad(Session *ss)
{
    return LoadResources(ss);
}

void SessionInit(Session *ss, const Platform *plat)
{
    Zero(ss, sizeof(*ss));
    ss->plat = plat;
    Plat = plat;
    ss->phase = RbPhaseLoad;
    ss->running = 1;
    ss->selected = 0;
    ss->bootIndex = -1;
    ss->fade = 0.f;
    ss->selAnim = 1.f;
    ss->bootLogFollow = 1;
    Bootlog(ss, "Session  start");
}

void SessionFree(Session *ss)
{
    int i;
    if (!ss) return;
    ImageFree(&ss->logo);
    for (i = 0; i < RbMaxTargets; i++) ImageFree(&ss->icons[i]);
    FontFree(&ss->fontTitle);
    FontFree(&ss->fontBody);
    FontFree(&ss->fontCaption);
    HandoffFree(&ss->handoff);
    BootlogRelease(ss);
    RoboRbFree(ss->fb.px);
    Zero(ss, sizeof(*ss));
}

static int KeyHit(const Input *in, int key)
{
    int i;
    for (i = 0; i < in->nkeys; i++)
        if (in->keys[i] == key) return 1;
    return 0;
}

static int ConfirmHit(const Input *in)
{
    int i;
    if (KeyHit(in, RbKeyEnter)) return 1;
    for (i = 0; i < in->nchars; i++)
        if (in->chars[i] == ' ') return 1;
    return 0;
}

static int PickerChord(const Session *ss)
{
    int i;
    uint32_t need = ss->catalog.pickerKey ? ss->catalog.pickerKey : RbModAlt;
    if (!(ss->input.mods & need)) return 0;
    for (i = 0; i < ss->input.nchars; i++) {
        int ch = ss->input.chars[i];
        if (ch == 'm' || ch == 'M') return 1;
    }
    return 0;
}

static int ConsumeHotkey(Session *ss)
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

static void PresentFull(Session *ss);
static void PaintKernelSplash(Session *ss);

static RoboRbStatus TryBoot(Session *ss, int idx)
{
    Target *t;
    RoboRbStatus st;
    if (idx < 0 || idx >= ss->catalog.ntargets) return RbErrNotfound;
    t = &ss->catalog.targets[idx];
        if (t->restricted && t->lock[0]) {
        Bootlog(ss, "Auth  volume locked");
        ss->phase = RbPhaseAuth;
        ss->authLen = 0;
        ss->authBuf[0] = 0;
        ss->authFail = 0;
        return RbOk;
    }
    st = LoaderPrepare(t, ss->plat->root, &ss->handoff);
    if (st != RbOk) {
        Bootlogf(ss, "Prepare failed  %s  (%d)", t->id, st);
        idx = PolicyResolveIndex(&ss->catalog, &ss->env, 1);
        if (idx < 0 || idx == ss->bootIndex) return st;
        ss->bootIndex = idx;
        ss->selected = idx;
        return TryBoot(ss, idx);
    }
    ss->handoff.debug = RoboRbDebugEnabled(&ss->env);
    if (RoboRbDebugEnabled(&ss->env)) {
        float tx, ty, tw, th;
        DebugTermGeom(ss, &tx, &ty, &tw, &th);
        ss->handoff.debugTermX = (int)tx;
        ss->handoff.debugTermY = (int)ty;
        ss->handoff.debugTermW = (int)tw;
        ss->handoff.debugTermH = (int)th;
        Bootlogf(ss, "Debug  term  %d,%d  %dx%d",
                    ss->handoff.debugTermX, ss->handoff.debugTermY,
                    ss->handoff.debugTermW, ss->handoff.debugTermH);
    }
    ss->handoff.splashBarX = ss->splashBarX;
    ss->handoff.splashBarY = ss->splashBarY;
    ss->handoff.splashBarW = ss->splashBarW;
    ss->handoff.splashBarH = ss->splashBarH;
    {
        uint32_t p = (uint32_t)(ss->loadVisual * (float)RoboBootProgressMax + 0.5f);
        if (p > RoboBootProgressHandoff)
            p = RoboBootProgressHandoff;
        if (ss->splashDone && p < RoboBootProgressHandoff)
            p = RoboBootProgressHandoff;
        ss->handoff.splashProgress = p;
    }
    if (!ss->splashBgReady)
        PaintKernelSplash(ss);
    Bootlog(ss, "Handoff  ExitBootServices next");
    if (RoboRbDebugEnabled(&ss->env))
        DebugFlush(ss);
    if (ss->catalog.saveLast) {
        Bootlogf(ss, "Env  save last_boot=%s", t->id);
        EnvSet(&ss->env, "last_boot", t->id);
        EnvSet(&ss->env, "next_boot", "");
        if (EnvSave(&ss->env) != RbOk)
            Bootlog(ss, "Env  save failed");
        else
            Bootlog(ss, "Env  saved");
    }
    if (t->kind == RbKindFirmware) {
        Bootlog(ss, "Jump  firmware setup");
        st = ss->plat->FirmwareSetup ? ss->plat->FirmwareSetup() : RbOk;
    } else if (t->kind == RbKindReboot) {
        Bootlog(ss, "Jump  reboot");
        st = ss->plat->reboot ? ss->plat->reboot() : RbOk;
    } else if (t->kind == RbKindHalt) {
        Bootlog(ss, "Jump  halt");
        st = ss->plat->halt ? ss->plat->halt() : RbOk;
    } else {
        Bootlog(ss, "Jump  kernel");
        st = ss->plat->boot ? ss->plat->boot(t, &ss->handoff) : RbOk;
    }
    /* Host cannot jump into the ELF; keep the window on the kernel log.
       EFI never returns from a successful EfiStartKernel(). */
    if (st == RbOk &&
        (t->kind == RbKindKernel || t->kind == RbKindLinux || t->kind == RbKindMultiboot2)) {
        if (RoboRbDebugEnabled(&ss->env)) {
            Bootlog(ss, "Kernel entered");
            Bootlogf(ss, "Framebuffer  %d x %d  32-bit", ss->fb.w, ss->fb.h);
            Bootlog(ss, "Memory  host map");
            Bootlog(ss, "CPU  x86_64 idle");
        }
        ss->phase = RbPhaseKlog;
        ss->running = 1;
        return st;
    }
    ss->phase = RbPhaseDone;
    ss->running = 0;
    return st;
}

static void PresentFull(Session *ss)
{
    if (!ss || !ss->plat || !ss->plat->present) return;
    FbDamageFull(&ss->fb);
    ss->plat->present(&ss->fb);
}

static void PaintKernelSplash(Session *ss)
{
    ss->loadVisual = RbSplashHandoff;
    ss->splashBgReady = 0;
    SplashDraw(ss, ss->loadVisual, 1.f);
    PresentFull(ss);
}

static void BeginBootSelected(Session *ss)
{
    ss->bootIndex = ss->selected;
    Bootlogf(ss, "Boot  %s  %s", ss->catalog.targets[ss->selected].id,
                KindName(ss->catalog.targets[ss->selected].kind));
    PaintKernelSplash(ss);
    Bootlogf(ss, "Loader  %u percent  kernel continues",
                (unsigned)(RbSplashHandoff * 100.f + 0.5f));
    if (RoboRbDebugEnabled(&ss->env)) {
        Bootlog(ss, "Phase  verbose hold");
        ss->phase = RbPhaseVerbose;
        ss->timeoutLeftMs = 900;
        return;
    }
    Bootlog(ss, "Paint  handoff");
    ss->phase = RbPhaseHandoff;
}

int SessionTick(Session *ss)
{
    uint64_t now;
    float dt;
    int i;
    if (!ss || !ss->running) return 0;
    now = ss->plat->NowMs();
    if (ss->lastMs == 0) ss->lastMs = now;
    dt = (float)(now - ss->lastMs) / 1000.f;
    if (dt < 0.f) dt = 0.f;
    if (dt > 0.05f) dt = 0.05f;
    ss->lastMs = now;

    Zero(&ss->input, sizeof(ss->input));
    ss->plat->PollInput(&ss->input);
    if (ss->input.quit) {
        ss->running = 0;
        return 0;
    }
    if (RoboRbDebugEnabled(&ss->env)) {
        int delta = 0;
        if (KeyHit(&ss->input, RbKeyUp)) delta = -1;
        else if (KeyHit(&ss->input, RbKeyDown)) delta = 1;
        if (delta && DebugLogScroll(ss, delta)) {
            if (ss->phase == RbPhasePicker && ss->catalog.pickerTimeoutS > 0)
                ss->timeoutLeftMs = ss->catalog.pickerTimeoutS * 1000;
        }
    }
    if (PickerChord(ss)) {
        if (!ss->altLatched)
            Bootlog(ss, "Alt+M detected");
        ss->altLatched = 1;
    }
    if (ss->phase == RbPhaseLoad || ss->phase == RbPhaseSplash) {
        if (KeyHit(&ss->input, RbKeyEsc) || KeyHit(&ss->input, RbKeyF8) ||
            KeyHit(&ss->input, RbKeyF10)) {
            if (!ss->altLatched)
                Bootlog(ss, "Picker key");
            ss->altLatched = 1;
        }
    }

    if (ss->phase != RbPhaseLoad && ss->phase != RbPhaseSplash) {
        ss->fb.damageW = 0;
        ss->fb.damageH = 0;
    }

    if (ss->phase == RbPhaseLoad || ss->phase == RbPhaseSplash) {
        if (!ss->splashInited) {
            ss->splashInited = 1;
            ss->phase = RbPhaseSplash;
            if (ss->loadReal < 1.f) {
                if (LoadResources(ss) != RbOk) {
                    ss->running = 0;
                    return 0;
                }
            }
            now = ss->plat->NowMs();
            ss->splashStartMs = now;
            ss->lastMs = now;
            Bootlogf(ss, "Splash  timer  %d ms  display %d x %d",
                        ss->catalog.splashMinMs, ss->fb.w, ss->fb.h);
        }
        {
            uint64_t elapsed = now - ss->splashStartMs;
            int done;
            ss->loadVisual = SplashVisual(ss->loadReal, elapsed, ss->catalog.splashMinMs) *
                              RbSplashHandoff;
            ss->fade = 1.f;
            done = SplashComplete(ss->loadReal, elapsed, ss->catalog.splashMinMs);
            if (!done && ss->loadReal >= 0.999f && ss->splashTicks >= 80)
                done = 1;
            if (done)
                ss->loadVisual = RbSplashHandoff;
            SplashDraw(ss, ss->loadVisual, 1.f);
            if (ss->plat->present) ss->plat->present(&ss->fb);
            ss->splashTicks++;
            if (done) {
                BootIntent intent = PolicyIntent(
                    ss->altLatched, ss->forceMenu, EnvGet(&ss->env, "next_boot"));
                ss->splashDone = 1;
                Bootlogf(ss, "Splash  done  %llu ms  intent %s",
                            (unsigned long long)elapsed,
                            intent == RbBootPicker ? "picker" :
                            intent == RbBootOnce ? "next_boot" : "default");
                if (ss->altLatched)
                    Bootlog(ss, "Splash  picker chord latched");
                if (ss->forceMenu)
                    Bootlog(ss, "Splash  menu flag");
                if (intent == RbBootPicker) {
                    Bootlog(ss, "Opening picker");
                    LoadPickerIcons(ss);
                    ss->phase = RbPhasePicker;
                    ss->fade = 1.f;
                    ss->selAnim = 1.f;
                    ss->pickerDrawn = 1;
                    Bootlog(ss, "Paint  picker");
                    PickerDraw(ss);
                    PresentFull(ss);
                } else {
                    ss->selected = PolicyResolveIndex(&ss->catalog, &ss->env, 0);
                    BeginBootSelected(ss);
                }
            }
        }
        return 1;
    }

    if (ss->phase == RbPhasePicker) {
        int prev = ss->selected;
        int dirty;
        ss->fade = 1.f;
        ss->selAnim = 1.f;
        if (KeyHit(&ss->input, RbKeyLeft)) {
            ss->selected = PolicyNextIndex(&ss->catalog, ss->selected, -1);
            ss->timeoutLeftMs = ss->catalog.pickerTimeoutS * 1000;
            Bootlogf(ss, "Selected  %s", ss->catalog.targets[ss->selected].id);
        }
        if (KeyHit(&ss->input, RbKeyRight)) {
            ss->selected = PolicyNextIndex(&ss->catalog, ss->selected, 1);
            ss->timeoutLeftMs = ss->catalog.pickerTimeoutS * 1000;
            Bootlogf(ss, "Selected  %s", ss->catalog.targets[ss->selected].id);
        }
        ConsumeHotkey(ss);
        for (i = 0; i < ss->input.nchars; i++) {
            int ch = ss->input.chars[i];
            if (ch == 's' || ch == 'S') {
                Bootlog(ss, "Settings");
                ss->phase = RbPhaseSettings;
                ss->fade = 1.f;
                ss->settingsDrawn = 0;
                break;
            }
        }
        if (ConfirmHit(&ss->input)) BeginBootSelected(ss);
        if (KeyHit(&ss->input, RbKeyF10) || KeyHit(&ss->input, RbKeyConsole)) {
            ss->phase = RbPhaseConsole;
            ss->consoleLen = 0;
            ss->consoleIn[0] = 0;
        }
        if (ss->catalog.pickerTimeoutS > 0) {
            ss->timeoutLeftMs -= (int)(dt * 1000.f);
            if (ss->timeoutLeftMs <= 0) {
                ss->selected = PolicyResolveIndex(&ss->catalog, &ss->env, 0);
                BeginBootSelected(ss);
            }
        }
        if (ss->phase != RbPhasePicker) return 1;
        dirty = !ss->pickerDrawn || ss->selected != prev;
        if (RoboRbDebugEnabled(&ss->env) && ss->bootLogSeq != ss->bootLogShown) dirty = 1;
        if (ss->catalog.pickerTimeoutS > 0) {
            int nowSec = (ss->timeoutLeftMs + 999) / 1000;
            int wasSec = (ss->timeoutLeftMs + (int)(dt * 1000.f) + 999) / 1000;
            if (nowSec != wasSec) dirty = 1;
        }
        if (dirty) {
            PickerDraw(ss);
            if (ss->plat->present) ss->plat->present(&ss->fb);
            ss->pickerDrawn = 1;
        }
        return 1;
    }

    if (ss->phase == RbPhaseSettings) {
        int dirty = !ss->settingsDrawn;
        ss->fade = 1.f;
        if (RoboRbDebugEnabled(&ss->env) && ss->bootLogSeq != ss->bootLogShown)
            dirty = 1;
        if (KeyHit(&ss->input, RbKeyEsc)) {
            EnvSave(&ss->env);
            ss->phase = RbPhasePicker;
            ss->pickerDrawn = 0;
            ss->fade = 1.f;
            return 1;
        }
        if (KeyHit(&ss->input, RbKeyEnter) || KeyHit(&ss->input, RbKeyRight) ||
            KeyHit(&ss->input, RbKeyLeft)) {
            DebugSet(&ss->env, !RoboRbDebugEnabled(&ss->env));
            Bootlog(ss, RoboRbDebugEnabled(&ss->env) ? "Debug on" : "Debug off");
            if (RoboRbDebugEnabled(&ss->env))
                Bootlog(ss, "Logs  Up/Down to scroll");
            if (EnvSave(&ss->env) != RbOk)
                Logf("debug setting not saved");
            dirty = 1;
        }
        if (dirty) {
            SettingsDraw(ss);
            if (ss->plat->present) ss->plat->present(&ss->fb);
            ss->settingsDrawn = 1;
        }
        return 1;
    }

    if (ss->phase == RbPhaseVerbose) {
        int dirty;
        ss->timeoutLeftMs -= (int)(dt * 1000.f);
        dirty = !ss->splashBgReady ||
                (RoboRbDebugEnabled(&ss->env) && ss->bootLogSeq != ss->bootLogShown);
        if (dirty) {
            VerboseDraw(ss);
            if (ss->plat->present) ss->plat->present(&ss->fb);
        }
        if (ss->timeoutLeftMs <= 0 || KeyHit(&ss->input, RbKeyEnter))
            ss->phase = RbPhaseHandoff;
        return 1;
    }

    if (ss->phase == RbPhaseKlog) {
        int dirty;
        if (KeyHit(&ss->input, RbKeyEsc)) {
            ss->running = 0;
            return 0;
        }
        dirty = !ss->splashBgReady || ss->bootLogSeq != ss->bootLogShown;
        if (dirty) {
            KlogDraw(ss);
            if (ss->plat->present) ss->plat->present(&ss->fb);
        }
        return 1;
    }

    if (ss->phase == RbPhaseAuth) {
        for (i = 0; i < ss->input.nchars; i++) {
            int ch = ss->input.chars[i];
            if (ch >= 32 && ch < 127 && ss->authLen < (int)sizeof(ss->authBuf) - 1) {
                ss->authBuf[ss->authLen++] = (char)ch;
                ss->authBuf[ss->authLen] = 0;
                ss->authFail = 0;
            }
        }
        if (KeyHit(&ss->input, RbKeyBackspace) && ss->authLen > 0) {
            ss->authBuf[--ss->authLen] = 0;
        }
        if (KeyHit(&ss->input, RbKeyEsc)) {
            ss->phase = RbPhasePicker;
            ss->authLen = 0;
            ss->pickerDrawn = 0;
        }
        if (KeyHit(&ss->input, RbKeyEnter)) {
            Target *t = &ss->catalog.targets[ss->selected];
            if (LockVerify(t->lock, ss->authBuf)) {
                t->restricted = 0;
                BeginBootSelected(ss);
            } else {
                ss->authFail = 1;
                ss->authLen = 0;
                ss->authBuf[0] = 0;
            }
        }
        AuthDraw(ss);
        if (ss->plat->present) ss->plat->present(&ss->fb);
        return 1;
    }

    if (ss->phase == RbPhaseConsole) {
        for (i = 0; i < ss->input.nchars; i++) {
            int ch = ss->input.chars[i];
            if (ch >= 32 && ch < 127 && ss->consoleLen < (int)sizeof(ss->consoleIn) - 1) {
                ss->consoleIn[ss->consoleLen++] = (char)ch;
                ss->consoleIn[ss->consoleLen] = 0;
            }
        }
        if (KeyHit(&ss->input, RbKeyBackspace) && ss->consoleLen > 0) {
            ss->consoleIn[--ss->consoleLen] = 0;
        }
        if (KeyHit(&ss->input, RbKeyEnter)) ConsoleSubmit(ss);
        if (KeyHit(&ss->input, RbKeyEsc)) {
            ss->phase = RbPhasePicker;
            ss->pickerDrawn = 0;
        }
        ConsoleDraw(ss);
        if (ss->plat->present) ss->plat->present(&ss->fb);
        return 1;
    }

    if (ss->phase == RbPhaseHandoff) {
        TryBoot(ss, ss->bootIndex);
        return ss->running;
    }

    return ss->running;
}

RoboRbStatus SessionBoot(Session *ss)
{
    while (SessionTick(ss)) {
        if (ss->plat->SleepMs) ss->plat->SleepMs(32);
    }
    return RbOk;
}
