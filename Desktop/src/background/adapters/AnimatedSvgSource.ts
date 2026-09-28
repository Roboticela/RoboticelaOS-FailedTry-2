/**
 * AnimatedSvgSource — adapter for SMIL / CSS-animated inline SVGs.
 *
 * The SVG must be inlined (not <img src="…">) so the DOM is accessible,
 * which is required for:
 *   pause()  → svgEl.pauseAnimations()  OR  toggle animation-play-state
 *   resume() → svgEl.unpauseAnimations() OR restore animation-play-state
 */

import { BackgroundSource, type FitMode } from "../BackgroundSource";

export class AnimatedSvgSource extends BackgroundSource {
  private readonly url: string;
  private wrapper: HTMLDivElement | null = null;
  private svgEl: SVGSVGElement | null = null;
  private fitMode: FitMode = "fill";
  private _paused = false;
  private rawSvg = "";

  constructor(url: string) {
    super();
    this.url = url;
  }

  async mount(container: HTMLElement): Promise<void> {
    // Fetch SVG source and inline it
    const response = await fetch(this.url);
    this.rawSvg = await response.text();

    const wrapper = document.createElement("div");
    wrapper.className = "bg-source bg-source--svg-animated";
    wrapper.innerHTML = this.rawSvg;

    const svgEl = wrapper.querySelector("svg");
    if (svgEl) {
      // Force the SVG to fill its wrapper
      svgEl.setAttribute("width", "100%");
      svgEl.setAttribute("height", "100%");
      svgEl.style.width = "100%";
      svgEl.style.height = "100%";
      svgEl.style.display = "block";
      this.svgEl = svgEl;
    }

    this.applyFit(wrapper, this.fitMode);
    container.appendChild(wrapper);
    this.wrapper = wrapper;

    if (this._paused) {
      this.pauseSvg();
    }
  }

  unmount(): void {
    this.wrapper?.remove();
    this.wrapper = null;
    this.svgEl = null;
  }

  pause(): void {
    this._paused = true;
    if (this.svgEl) this.pauseSvg();
  }

  resume(): void {
    this._paused = false;
    if (this.svgEl) this.resumeSvg();
  }

  setFitMode(mode: FitMode): void {
    this.fitMode = mode;
    if (this.wrapper) this.applyFit(this.wrapper, mode);
  }

  async getPosterFrame(): Promise<string> {
    if (!this.svgEl) return "";
    const svgString = this.svgEl.outerHTML;
    const blob = new Blob([svgString], { type: "image/svg+xml" });
    return URL.createObjectURL(blob);
  }

  isAnimated(): boolean {
    return true;
  }

  // ── Private ────────────────────────────────────────────────────────────────

  private pauseSvg(): void {
    const svg = this.svgEl;
    if (!svg) return;
    // SMIL animations
    if (typeof svg.pauseAnimations === "function") {
      svg.pauseAnimations();
    }
    // CSS animations on all child elements
    svg.querySelectorAll<HTMLElement | SVGElement>("*").forEach((el) => {
      (el as HTMLElement).style.animationPlayState = "paused";
    });
  }

  private resumeSvg(): void {
    const svg = this.svgEl;
    if (!svg) return;
    if (typeof svg.unpauseAnimations === "function") {
      svg.unpauseAnimations();
    }
    svg.querySelectorAll<HTMLElement | SVGElement>("*").forEach((el) => {
      (el as HTMLElement).style.animationPlayState = "running";
    });
  }

  private applyFit(el: HTMLDivElement, mode: FitMode): void {
    const svg = el.querySelector("svg");
    if (!svg) return;

    switch (mode) {
      case "fill":
        svg.style.objectFit = "cover";
        el.style.overflow = "hidden";
        break;
      case "fit":
        svg.style.objectFit = "contain";
        break;
      case "stretch":
        svg.style.width = "100%";
        svg.style.height = "100%";
        svg.removeAttribute("viewBox");
        break;
      case "center":
        el.style.display = "flex";
        el.style.alignItems = "center";
        el.style.justifyContent = "center";
        svg.style.width = "auto";
        svg.style.height = "auto";
        break;
      case "tile":
        el.style.backgroundImage = `url("data:image/svg+xml,${encodeURIComponent(this.rawSvg)}")`;
        el.style.backgroundRepeat = "repeat";
        svg.style.display = "none";
        break;
    }
  }
}
