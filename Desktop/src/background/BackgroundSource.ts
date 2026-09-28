/**
 * Universal Background Engine — Core Interface
 *
 * Every media-type adapter (image, GIF, SVG, video, generative) implements
 * this contract so consumers never need to know which kind they're holding.
 */

export type FitMode = "fill" | "fit" | "stretch" | "center" | "tile";

export abstract class BackgroundSource {
  /** Attach the source's DOM / canvas into the given container. */
  abstract mount(container: HTMLElement): Promise<void>;

  /** Detach, clean up listeners, stop render loops. */
  abstract unmount(): void;

  /**
   * Freeze animation at the current frame.
   * Must be a no-op (not throw) for static sources.
   */
  abstract pause(): void;

  /**
   * Continue from the exact frame where pause() was called.
   * Must never restart from t=0.
   */
  abstract resume(): void;

  /**
   * Apply a fit mode. Implementations apply whichever CSS property is
   * appropriate for their rendering method (object-fit vs background-size).
   */
  abstract setFitMode(mode: FitMode): void;

  /**
   * Returns a data-URL or blob-URL of a representative static frame.
   * Used by Settings previews, Lock Screen thumbnails, Low-Power fallback.
   */
  abstract getPosterFrame(): Promise<string>;

  /** true for video / GIF / animated-SVG / live-scene; false for static. */
  abstract isAnimated(): boolean;
}
