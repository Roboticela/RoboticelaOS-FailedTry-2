/*
 * Guest application documents — static markup for RoboCSS / the OS scene.
 *
 * Native chrome (panel, window frames) owns the desk. These trees are **content only**:
 * no nested wallpaper, menu bar, dock, or traffic lights. `export-page.mjs` SSR-renders
 * each app into DesktopPage.h. The interactive Vite lab lives under `components/interactive/`.
 */

import type { ReactElement } from "react";
import { CommandButton, commandProps } from "@roboticela/react";

import { buildDesktopModel, type DesktopModel } from "@/session/model";
import { renderDesktopStyleSheet } from "@/theme/stylesheet";

const phaseRows = [
  { id: "p12", label: "Typography · RoboText", detail: "Shaping, measurement, paint bridge" },
  { id: "p13", label: "Input", detail: "Keys, pointer, focus, clipboard" },
  { id: "p15", label: "Compositor", detail: "Layers, damage, present" },
  { id: "p16", label: "RoboJS", detail: "Page script on the document" },
  { id: "p17", label: "Complete HTML", detail: "DOM, forms, navigation" },
  { id: "p18", label: "Complete CSS", detail: "Flex, cascade, media" },
];

/** System Overview — product checklist for the stack that is live. */
export function GuestOverviewApp(): ReactElement {
  return (
    <div className="appRoot" id="overviewRoot" role="document" aria-label="System Overview">
      <div className="appHeader">
        <div className="brandMark">RoboticelaOS</div>
        <div className="appEyebrow">System Overview</div>
      </div>
      <div className="appLead">
        The runtime that ships: text, input, composition, script, and a complete CSS surface — drawn by
        the native shell, not a nested desk.
      </div>
      <div className="phaseList" role="list" aria-label="Landed phases">
        {phaseRows.map((row) => (
          <div key={row.id} className="phaseRow" role="listitem">
            <div className="phaseLabel">{row.label}</div>
            <div className="phaseDetail">{row.detail}</div>
          </div>
        ))}
      </div>
      <div className="appActions">
        <CommandButton
          className="buttonPrimary"
          id="overviewOpenLab"
          aria-label="Open Input Lab"
          verb="app.openInputLab"
        >
          Open Input Lab
        </CommandButton>
        <CommandButton className="button" aria-label="About" verb="system.about">
          About
        </CommandButton>
      </div>
      <div className="appStatus" id="overviewStatus">
        Ready
      </div>
      <div className="jsBadge" id="overviewJsBadge">
        Script idle
      </div>
    </div>
  );
}

/** Input Lab — specimen controls for keyboard and pointer, not a fake OS. */
export function GuestInputLabApp(): ReactElement {
  return (
    <div className="appRoot" id="inputLabRoot" role="document" aria-label="Input Lab">
      <div className="appHeader">
        <div className="brandMark">RoboticelaOS</div>
        <div className="appEyebrow">Input Lab</div>
      </div>
      <div className="appLead">
        Press keys to update the panel status. Click a title to raise a window. Escape ends the session.
      </div>

      <div className="sectionLabel">Text fields</div>
      <div className="field" role="textbox" aria-label="Empty field">
        Empty
      </div>
      <div className="fieldFocused" role="textbox" aria-label="Focused field">
        Focused — type to update status
      </div>
      <div className="fieldDisabled" role="textbox" aria-disabled="true" aria-label="Disabled field">
        Disabled
      </div>
      <div className="fieldError" role="textbox" aria-invalid="true" aria-label="Error field">
        Needs a value
      </div>

      <div className="sectionLabel">Buttons</div>
      <div className="appRow">
        <CommandButton
          className="buttonPrimary"
          id="labPrimary"
          aria-label="Primary"
          verb="lab.primary"
        >
          Primary
        </CommandButton>
        <CommandButton className="button" aria-label="Secondary" verb="lab.secondary">
          Secondary
        </CommandButton>
      </div>
      <div className="appRow">
        <div className="buttonActive" role="button" aria-pressed="true" aria-label="Pressed">
          Pressed
        </div>
        <div className="buttonDisabled" role="button" aria-disabled="true" aria-label="Disabled">
          Disabled
        </div>
      </div>

      <div className="sectionLabel">Toggles and checks</div>
      <div className="appRow">
        <div
          className="toggleOn"
          role="switch"
          aria-checked="true"
          aria-label="On"
          {...commandProps("lab.toggle")}
        />
        <div
          className="toggle"
          role="switch"
          aria-checked="false"
          aria-label="Off"
          {...commandProps("lab.toggle")}
        />
        <div
          className="checkboxOn"
          role="checkbox"
          aria-checked="true"
          aria-label="Checked"
          {...commandProps("lab.check")}
        />
        <div
          className="checkbox"
          role="checkbox"
          aria-checked="false"
          aria-label="Unchecked"
          {...commandProps("lab.check")}
        />
      </div>

      <div className="sectionLabel">Slider</div>
      <div className="sliderTrack">
        <div className="sliderFill" />
      </div>

      <div className="sectionLabel">Pointer and keyboard</div>
      <div className="labBadge">Mouse · raise · close · cursor</div>
      <div className="labBadge">Keyboard · LogicalKey · modifiers · Escape</div>
      <div className="caption">Live key names appear in the panel status on every KeyDown.</div>

      <div className="appStatus" id="labStatus">
        Ready
      </div>
      <div className="jsBadge" id="labJsBadge">
        Script idle
      </div>
    </div>
  );
}

/** @deprecated Prefer GuestOverviewApp / GuestInputLabApp — kept for callers that expect one root. */
export function GuestDesktopShell({ model = buildDesktopModel() }: { model?: DesktopModel }): ReactElement {
  void model;
  return <GuestOverviewApp />;
}

export function desktopStyleSheetOptions(_model?: DesktopModel) {
  return { appearance: "light" as const };
}

export function renderGuestPage(model = buildDesktopModel()) {
  return {
    model,
    style: renderDesktopStyleSheet(desktopStyleSheetOptions(model)),
  };
}
