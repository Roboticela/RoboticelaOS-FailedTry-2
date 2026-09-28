/**
 * BackgroundLayer — the universal background container React component.
 *
 * Drop this wherever you need a background surface (Desktop, Lock Screen,
 * Screensaver, Settings preview).  It renders a full-coverage container
 * that the BackgroundEngine attaches its DOM into.
 *
 * Props:
 *   source       – a BackgroundSource instance; changing this swaps the
 *                  wallpaper with a crossfade transition.
 *   fitMode      – 'fill' | 'fit' | 'stretch' | 'center' | 'tile'
 *   lowPower     – mirrors the system-wide Low-Power Mode flag
 *   visible      – false when a Space is not active (pauses, never unmounts)
 *   className    – additional classes for the outer container
 *   crossfade    – duration in ms; 0 to disable (default 500)
 *
 * Example:
 *   <BackgroundLayer source={generativeSource} fitMode="fill" />
 */

import { useEffect, useRef } from "react";
import { useBackgroundEngine } from "./useBackgroundEngine";
import type { BackgroundSource } from "./BackgroundSource";
import type { FitMode } from "./BackgroundSource";

export interface BackgroundLayerProps {
  source?: BackgroundSource;
  fitMode?: FitMode;
  lowPower?: boolean;
  visible?: boolean;
  className?: string;
  crossfade?: number;
  "data-testid"?: string;
}

export function BackgroundLayer({
  source,
  fitMode = "fill",
  lowPower = false,
  visible = true,
  className = "",
  crossfade = 500,
  "data-testid": testId,
}: BackgroundLayerProps) {
  const { ref, setSource, setFitMode, setLowPower, setVisible } = useBackgroundEngine();

  // Track previous source to avoid re-mounting on every render
  const prevSourceRef = useRef<BackgroundSource | undefined>(undefined);

  useEffect(() => {
    if (source && source !== prevSourceRef.current) {
      prevSourceRef.current = source;
      setSource(source, crossfade > 0);
    }
  }, [source, crossfade, setSource]);

  useEffect(() => {
    setFitMode(fitMode);
  }, [fitMode, setFitMode]);

  useEffect(() => {
    setLowPower(lowPower);
  }, [lowPower, setLowPower]);

  useEffect(() => {
    setVisible(visible);
  }, [visible, setVisible]);

  return (
    <div
      ref={ref}
      data-testid={testId}
      data-fit={fitMode}
      className={`bg-layer ${className}`}
      aria-hidden="true"
    />
  );
}
