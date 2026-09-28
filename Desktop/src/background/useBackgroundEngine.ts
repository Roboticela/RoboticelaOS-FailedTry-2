/**
 * useBackgroundEngine — React hook that owns a BackgroundEngine instance
 * tied to a ref container.
 *
 * Returns:
 *   - ref:           attach to the container <div>
 *   - engine:        the BackgroundEngine (for imperative calls)
 *   - setSource:     async setter — swaps source with crossfade
 *   - setFitMode:    sets fit on the current source immediately
 *   - setLowPower:   enables/disables Low-Power mode
 *   - setVisible:    pauses/resumes (for Spaces navigation)
 */

import { useEffect, useRef, useCallback } from "react";
import { BackgroundEngine } from "./BackgroundEngine";
import { type BackgroundSource, type FitMode } from "./BackgroundSource";

export function useBackgroundEngine() {
  const containerRef = useRef<HTMLDivElement | null>(null);
  const engineRef = useRef<BackgroundEngine | null>(null);

  useEffect(() => {
    const el = containerRef.current;
    if (!el) return;

    const engine = new BackgroundEngine(el);
    engineRef.current = engine;

    return () => {
      engine.unmountAll();
      engineRef.current = null;
    };
  }, []);

  const setSource = useCallback(
    async (source: BackgroundSource, crossfade = true) => {
      const engine = engineRef.current;
      if (!engine) return;
      if (crossfade) {
        await engine.transitionTo(source, 500);
      } else {
        await engine.setSource(source);
      }
    },
    []
  );

  const setFitMode = useCallback((mode: FitMode) => {
    engineRef.current?.setFitMode(mode);
  }, []);

  const setLowPower = useCallback((enabled: boolean) => {
    engineRef.current?.setLowPowerMode(enabled);
  }, []);

  const setVisible = useCallback((visible: boolean) => {
    engineRef.current?.setVisible(visible);
  }, []);

  return {
    ref: containerRef,
    engine: engineRef,
    setSource,
    setFitMode,
    setLowPower,
    setVisible,
  };
}
