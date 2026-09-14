# RoboDisplay

Platform-independent display abstraction for RoboticelaOS Runtime.

**Version:** 0.1.0 (`ROBO_DISPLAY_API_FROZEN=0`)  
**Goal:** Phase 7.3

## What this is

RoboDisplay describes physical/logical displays (identity, geometry, scale/DPI,
refresh, orientation, primary status) and defines the backend boundary for
future host enumeration.

It does **not** yet:

- Enumerate real Win32 / X11 / Wayland / Cocoa monitors
- Present framebuffers or windows
- Handle VSync, hotplug, or mode switching

## Layout

| Path | Role |
|------|------|
| `Include/RoboDisplay/` | Public API |
| `Source/RoboDisplay/` | Manager + null/placeholder backends |
| `display/docs/` | Architecture notes |
| `display/tests/` | Host tests (`display.*`) + mock backend |

## Quick use

```c
robo_display_init();
robo_display_backend_use_placeholder(); /* or register a mock / future OS backend */
robo_display_refresh();

robo_display_info_t primary;
if (robo_display_primary(&primary) == ROBO_OK) {
    /* primary.bounds, primary.scale, ... */
}

robo_display_shutdown();
```

`robo_window_init` registers the placeholder backend so windows keep a primary
display association without host monitors.

See `display/docs/architecture.md`.
