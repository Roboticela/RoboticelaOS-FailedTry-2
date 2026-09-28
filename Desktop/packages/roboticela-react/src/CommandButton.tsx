import type { CSSProperties, ReactElement, ReactNode } from "react";

import { commandProps, type DesktopBridgeVerb } from "./bridge";

export type CommandButtonProps = {
  verb: DesktopBridgeVerb;
  children: ReactNode;
  className?: string;
  id?: string;
  role?: string;
  "aria-label"?: string;
  style?: CSSProperties;
};

/**
 * Content-only control that stamps `data-command`. Guest RoboJS reads it;
 * DesktopBridge runs the matching C verb. No onClick handlers in export markup.
 */
export function CommandButton(props: CommandButtonProps): ReactElement {
  const { verb, children, className, id, role = "button", style, ...rest } = props;
  return (
    <div
      className={className}
      id={id}
      role={role}
      style={style}
      aria-label={rest["aria-label"]}
      {...commandProps(verb)}
    >
      {children}
    </div>
  );
}
