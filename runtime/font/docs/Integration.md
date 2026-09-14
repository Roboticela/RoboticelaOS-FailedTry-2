# Future Integration Contracts (post–Phase 5)

RoboFont API v1 is frozen. Later phases **consume** it; they do not reimplement
font parsing.

## RoboText (Phase 6)

```text
Unicode / script runs (RoboUnicode)
        ↓
font selection / fallback (RoboFont)
        ↓
font instance (+ variation coords)
        ↓
cmap / metrics / outlines / color capabilities
        ↓
shaping (GSUB/GPOS) — NOT in RoboFont
```

## RoboCSS (Phase 9)

```text
font-family / style / weight / stretch / variation settings
        ↓
RoboFontSelector + generic families
        ↓
selected face / instance
```

## RoboLayout (Phase 10)

```text
font metrics + glyph advances
        ↓
line boxes / baselines / measurement
```

## RoboGraphics / RoboPaint (Phases 7 / 11)

```text
glyph outline / color glyph representation
        ↓
rasterization / paint (NOT in RoboFont)
```

## Explicitly deferred

Shaping, rasterization, CSS cascade integration, compositor font caches,
browser web-font fetch policy, OS font-manager as sole discovery backend.
