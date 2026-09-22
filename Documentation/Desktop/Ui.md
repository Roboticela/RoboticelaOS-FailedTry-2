# Desktop UI

The OS desktop **window document** is authored in `Desktop/` as a Vite + React tree. It is not executed as React in the guest.

On compile, `Scripts/Build/BuildDesktopUi.ps1` does three things:

1. `vite build` produces a static preview under `Build/Desktop/Ui/`.
2. `renderToStaticMarkup` of `DesktopShell` writes plain HTML and CSS.
3. Those strings are emitted as `Runtime/Source/RoboDesktop/Generated/DesktopPage.h`, which `RoboDesktopScene.c` includes.

CSS must stay inside the properties RoboCSS registers today: `display: block`, colours, font-size, line-height, margin, padding, background. Flex, grid and absolute positioning are not laid out in this build.

Guest JavaScript is Phase 13. The compiled `desktop.js` is kept as an artifact; the OS paints the prerendered HTML/CSS.

Playable input (mouse, software cursor, Escape to leave) is owned by RoboWindow + RoboDesktop, not by the React tree. The menu bar status label shows live pointer coordinates while you test devices.
