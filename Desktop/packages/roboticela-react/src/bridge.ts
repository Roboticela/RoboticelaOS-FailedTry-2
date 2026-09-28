/**
 * DesktopBridge verb spellings — must match Runtime/Source/RoboDesktop/RoboDesktopBridge.c.
 * Authors stamp these via commandProps / CommandButton; C executes them after RoboJS.
 */

export const DESKTOP_BRIDGE_VERBS = [
  "system.about",
  "app.openInputLab",
  "lab.primary",
  "lab.secondary",
  "lab.toggle",
  "lab.check",
] as const;

export type DesktopBridgeVerb = (typeof DESKTOP_BRIDGE_VERBS)[number];

export function isDesktopBridgeVerb(value: string): value is DesktopBridgeVerb {
  return (DESKTOP_BRIDGE_VERBS as readonly string[]).includes(value);
}

/** Props for a guest-export node that invokes a C bridge verb on click. */
export function commandProps(verb: DesktopBridgeVerb): { "data-command": DesktopBridgeVerb } {
  return { "data-command": verb };
}
