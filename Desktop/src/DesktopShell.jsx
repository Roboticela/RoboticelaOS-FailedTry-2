import React from "react";

/**
 * Guest document for the desktop window.
 * CSS must stay within RoboCSS today: block, colours, font-size, line-height,
 * margin, padding, background. No flex/grid/absolute.
 */
export function DesktopShell() {
  return (
    <div className="page">
      <div className="brand">RoboticelaOS</div>
      <div className="hero">Desktop</div>
      <div className="lede">
        Move the pointer. Click the close button. Press Escape to leave. The menu bar shows live coordinates.
      </div>

      <div className="section">
        <div className="sectionTitle">Pointer</div>
        <div className="sectionBody">
          Drag across the desktop and this window. The software cursor should track in realtime. Click the red button in the title bar to request a close.
        </div>
      </div>

      <div className="section">
        <div className="sectionTitle">Keyboard</div>
        <div className="sectionBody">
          PS/2 and USB HID keyboards are wired into the shell. Escape finishes this scene. Letter keys are queued for Phase 12 text input.
        </div>
      </div>

      <div className="section">
        <div className="sectionTitle">Devices</div>
        <div className="chip">PS/2 mouse</div>
        <div className="chip">PS/2 keyboard</div>
        <div className="chip">USB HID</div>
        <div className="chip">AHCI / NVMe</div>
        <div className="chip">xHCI</div>
        <div className="chip">GOP display</div>
      </div>

      <div className="cta">Ready</div>
      <div className="footnote">Generic drivers. No vendor GPU package required.</div>
    </div>
  );
}
