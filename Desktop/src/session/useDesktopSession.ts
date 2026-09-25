import { useCallback, useEffect, useMemo, useState } from "react";

import { buildDesktopModel, type DesktopModel } from "@/session/model";
import {
  desktop,
  ShellSurface,
  WindowState,
  type DesktopState,
  type DesktopWindow,
} from "@/state/desktopState";

export interface InputTelemetry {
  lastKey: string;
  lastMouse: string;
  pointer: { x: number; y: number; buttons: number };
  focusTarget: string;
}

export interface DesktopController {
  state: DesktopState;
  icons: DesktopModel["icons"];
  menu: DesktopModel["menu"];
  telemetry: InputTelemetry;
  painted: DesktopWindow[];
  focusedId: string | null;
  focus: (id: string) => void;
  close: (id: string) => void;
  minimize: (id: string) => void;
  maximize: (id: string) => void;
  move: (id: string, x: number, y: number) => void;
  openMenu: () => void;
  closeSurface: () => void;
  launch: (applicationId: string) => void;
  setStatus: (text: string) => void;
  setFocusTarget: (name: string) => void;
  status: string;
}

export function useDesktopSession(initialWidth = 1280, initialHeight = 800): DesktopController {
  const seed = useMemo(
    () =>
      buildDesktopModel({
        display: { width: initialWidth, height: initialHeight, scale: 1 },
      }),
    [initialWidth, initialHeight]
  );

  const [state, setState] = useState<DesktopState>(seed.state);
  const [status, setStatus] = useState("Ready — click, type, drag");
  const [telemetry, setTelemetry] = useState<InputTelemetry>({
    lastKey: "—",
    lastMouse: "—",
    pointer: { x: 0, y: 0, buttons: 0 },
    focusTarget: "desktop",
  });

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      const mods = [
        event.shiftKey ? "Shift" : null,
        event.ctrlKey ? "Ctrl" : null,
        event.altKey ? "Alt" : null,
        event.metaKey ? "Meta" : null,
      ]
        .filter(Boolean)
        .join("+");
      const label = mods ? `${mods}+${event.key}` : event.key;
      setTelemetry((current) => ({ ...current, lastKey: label }));
      setStatus(`key ${label}`);
      if (event.key === "Escape") {
        setState((current) => desktop.menu.close(current));
      }
    };

    const onPointerMove = (event: PointerEvent) => {
      setTelemetry((current) => ({
        ...current,
        pointer: { x: event.clientX, y: event.clientY, buttons: event.buttons },
        lastMouse: event.buttons ? `drag ${event.clientX},${event.clientY}` : `move ${event.clientX},${event.clientY}`,
      }));
      setState((current) => desktop.pointer.move(current, event.clientX, event.clientY));
    };

    const onPointerDown = (event: PointerEvent) => {
      setTelemetry((current) => ({
        ...current,
        lastMouse: `down button=${event.button} @ ${event.clientX},${event.clientY}`,
        pointer: { x: event.clientX, y: event.clientY, buttons: event.buttons },
      }));
    };

    const onPointerUp = (event: PointerEvent) => {
      setTelemetry((current) => ({
        ...current,
        lastMouse: `up button=${event.button} @ ${event.clientX},${event.clientY}`,
        pointer: { x: event.clientX, y: event.clientY, buttons: event.buttons },
      }));
    };

    window.addEventListener("keydown", onKeyDown);
    window.addEventListener("pointermove", onPointerMove);
    window.addEventListener("pointerdown", onPointerDown);
    window.addEventListener("pointerup", onPointerUp);
    return () => {
      window.removeEventListener("keydown", onKeyDown);
      window.removeEventListener("pointermove", onPointerMove);
      window.removeEventListener("pointerdown", onPointerDown);
      window.removeEventListener("pointerup", onPointerUp);
    };
  }, []);

  const painted = useMemo(
    () => desktop.paintOrder(state).filter((record) => record.state !== WindowState.Minimized),
    [state]
  );

  const focus = useCallback((id: string) => {
    setState((current) => desktop.window.focus(current, id, "pointer"));
    setStatus(`focus ${id}`);
  }, []);

  const close = useCallback((id: string) => {
    setState((current) => desktop.window.close(current, id));
    setStatus(`closed ${id}`);
  }, []);

  const minimize = useCallback((id: string) => {
    setState((current) => desktop.window.minimize(current, id));
    setStatus(`minimized ${id}`);
  }, []);

  const maximize = useCallback((id: string) => {
    setState((current) => desktop.window.maximize(current, id));
    setStatus(`zoomed ${id}`);
  }, []);

  const move = useCallback((id: string, x: number, y: number) => {
    setState((current) => desktop.window.move(current, id, x, y));
  }, []);

  const openMenu = useCallback(() => {
    setState((current) => desktop.menu.open(current, ShellSurface.ApplicationMenu, { x: 96, y: 28 }, 0));
    setStatus("menu open");
  }, []);

  const closeSurface = useCallback(() => {
    setState((current) => desktop.menu.close(current));
    setStatus("menu closed");
  }, []);

  const launch = useCallback((applicationId: string) => {
    setState((current) => {
      const existing = current.windows.find((window) => window.applicationId === applicationId);
      if (existing) {
        if (existing.state === WindowState.Minimized) {
          return desktop.window.focus(desktop.window.restore(current, existing.id), existing.id, "dock");
        }
        return desktop.window.focus(current, existing.id, "dock");
      }
      return desktop.window.open(current, {
        applicationId,
        title: applicationId.split(".").pop() ?? applicationId,
        x: 160 + current.windows.length * 24,
        y: 120 + current.windows.length * 24,
        width: 480,
        height: 360,
        content: applicationId.includes("input") ? "inputlab" : "overview",
      });
    });
    setStatus(`launch ${applicationId}`);
  }, []);

  const setFocusTarget = useCallback((name: string) => {
    setTelemetry((current) => ({ ...current, focusTarget: name }));
  }, []);

  return {
    state,
    icons: seed.icons,
    menu: seed.menu,
    telemetry,
    painted,
    focusedId: state.focusedWindow,
    focus,
    close,
    minimize,
    maximize,
    move,
    openMenu,
    closeSurface,
    launch,
    setStatus,
    setFocusTarget,
    status,
  };
}

