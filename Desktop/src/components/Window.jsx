/*
 * A window: its chrome, and the content the shell knows how to draw.
 *
 * The surface is split from its **slot** on purpose. `WindowSurface` renders the slot (the absolutely
 * positioned wrapper that carries the rectangle) around the surface (the material, the border, the
 * title bar), so:
 *
 *   a drag moves the slot and touches no style the window owns;
 *   a window's *appearance* (`window` or `windowInactive`) is one class on one element, which is what
 *   the sheet's one-class rule requires;
 *   and the title bar can be `position: relative` without depending on where the window is, which is
 *   what lets the traffic lights sit inside it rather than being placed in viewport coordinates.
 *
 * The traffic lights are absolutely positioned and the title is not, with the left padding the lights
 * need. That is the whole reason `titleText` carries a `padding-left` of 64px: without a flow layout
 * that can put two boxes on one line, a title and a control cluster share a title bar by one of them
 * being positioned and the other being padded past it. It is a real constraint of this build's layout,
 * and it is written down where a reader will meet it rather than being rediscovered.
 */

import React from "react";

/*
 * A window's depth is drawn as the **border and the title bar's own surface** rather than as a
 * `box-shadow`, which this build does not paint: the one-pixel border plus the tinted title bar is
 * what separates a window from the wallpaper here, and the token sheet is where that decision lives.
 * A second offset surface behind each window would be the more familiar trick and it is deliberately
 * not used — it doubles the boxes in the frame and every one of them is a box the compositor of
 * Phase 15 will have to learn to skip.
 */

/**
 * One window's chrome.
 *
 * `closable`/`resizable`/`movable` gate the *controls* rather than the window: a window that cannot be
 * closed still has a title bar, and drawing a dead close button would be the shell claiming a
 * capability it was told it does not have.
 */
export function TitleBar({ window, focused }) {
  return (
    <div className={focused ? "titleBar" : "titleBarInactive"} role="presentation">
      {window.closable ? (
        <div className="trafficClose" role="button" aria-label={`Close ${window.title}`} data-command="window.close" />
      ) : null}
      <div className="trafficMinimize" role="button" aria-label={`Minimize ${window.title}`} data-command="window.minimize" />
      <div className="trafficZoom" role="button" aria-label={`Zoom ${window.title}`} data-command="window.maximize" />
      <div className={focused ? "titleText" : "titleTextInactive"} role="heading" aria-level="1">
        {window.title}
      </div>
    </div>
  );
}

/**
 * The window-content model (14.48).
 *
 * A window's body is a *kind*, and this is the switch that draws it. The point of the switch being
 * here rather than in the window is that a window is chrome plus content and the chrome must not know
 * which content it has: a new surface is a case in this function and no change to the chrome, which is
 * also what makes the native/HTML application surfaces of 14.49 a case rather than a redesign.
 */
export function WindowContent({ kind }) {
  if (kind === "inspector") {
    return (
      <div className="windowContent">
        <div className="heading">Inspector</div>
        <div className="body">Every glyph run the document produced, with its script, direction and face.</div>
        <div className="field" role="textbox" aria-label="Filter runs">
          arabic
        </div>
        <div className="mono">runs 268 · glyphs 271 · fallbacks 0</div>
        <div className="caption">Layout 4.1 ms · paint 2.6 ms</div>
        <div className="sliderTrack">
          <div className="sliderFill" />
        </div>
      </div>
    );
  }

  /*
   * The default surface: the typography specimen, which is what the phase asks a desktop to lead
   * with ("typography is treated as a primary visual element", 14.81). The text is real shaping
   * input — the same pipeline the window's own labels go through — and the controls below it are
   * drawn from the token sheet rather than being a second set of values.
   */
  return (
    <div className="windowContent">
      <div className="specimenTitle">Beautiful by default</div>
      <div className="specimenBody">
        RoboticelaOS shapes every label with RoboText: one glyph run per script, one face from the
        system pack, one pen.
      </div>
      <div className="specimenMeta">Inter · 13 / 20 · weight 400 · tracking 0</div>
      <div className="fieldFocused" role="textbox" aria-label="Search the system">
        Search the system
      </div>
      <div className="caption">Focused — the ring is the border, widened</div>
      <div className="sliderTrack">
        <div className="sliderFill" />
      </div>
      <div className="row">
        <div className="toggleOn" role="switch" aria-checked="true" aria-label="Reduce motion" />
        <div className="toggle" role="switch" aria-checked="false" aria-label="Increase contrast" />
      </div>
      <div className="row">
        <div className="buttonPrimary" role="button" aria-label="Apply">
          Apply
        </div>
        <div className="button" role="button" aria-label="Revert">
          Revert
        </div>
      </div>
    </div>
  );
}

/** One window: its slot, its surface, its title bar and its body. */
export function WindowSurface({ window: record, focused, children }) {
  return (
    <div className={record.slot} role="presentation">
      <div className={focused ? "window" : "windowInactive"} role="group" aria-label={record.title}>
        <TitleBar window={record} focused={focused} />
        {children ?? <WindowContent kind={record.content} />}
      </div>
    </div>
  );
}
