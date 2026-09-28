/**
 * @roboticela/react — host authoring for RoboticelaOS desktop documents.
 *
 * C owns behavior (DesktopBridge / WM). HTML/CSS owns look. This package stamps
 * bridge verbs and is SSR'd into DesktopPage.h — it never ships a guest React runtime.
 */

export {
  DESKTOP_BRIDGE_VERBS,
  commandProps,
  isDesktopBridgeVerb,
  type DesktopBridgeVerb,
} from "./bridge";

export { CommandButton, type CommandButtonProps } from "./CommandButton";
