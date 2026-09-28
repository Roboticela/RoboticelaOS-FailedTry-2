/**
 * VideoSource — adapter for looping background video (mp4 / webm / mov).
 *
 * Rendering path: <video autoplay muted loop playsinline>.
 * pause() / resume() map to HTMLVideoElement.pause() / .play().
 * Fit mode: object-fit / object-position on the element.
 */

import { BackgroundSource, type FitMode } from "../BackgroundSource";

const FIT_OBJECT: Record<FitMode, string> = {
  fill: "cover",
  fit: "contain",
  stretch: "fill",
  center: "none",
  tile: "none",
};

export class VideoSource extends BackgroundSource {
  private readonly url: string;
  private video: HTMLVideoElement | null = null;
  private fitMode: FitMode = "fill";
  private _paused = false;

  constructor(url: string) {
    super();
    this.url = url;
  }

  async mount(container: HTMLElement): Promise<void> {
    const video = document.createElement("video");
    video.className = "bg-source bg-source--video";
    video.src = this.url;
    video.autoplay = true;
    video.muted = true;
    video.loop = true;
    video.playsInline = true;
    video.setAttribute("playsinline", "");
    video.disablePictureInPicture = true;
    this.applyFit(video, this.fitMode);
    container.appendChild(video);
    this.video = video;

    await new Promise<void>((resolve) => {
      video.addEventListener("canplay", () => resolve(), { once: true });
      video.addEventListener("error", () => resolve(), { once: true });
    });

    if (!this._paused) {
      video.play().catch(() => {/* browsers may block autoplay; silently ignore */});
    }
  }

  unmount(): void {
    if (this.video) {
      this.video.pause();
      this.video.src = "";
      this.video.remove();
      this.video = null;
    }
  }

  pause(): void {
    this._paused = true;
    this.video?.pause();
  }

  resume(): void {
    this._paused = false;
    this.video?.play().catch(() => {});
  }

  setFitMode(mode: FitMode): void {
    this.fitMode = mode;
    if (this.video) this.applyFit(this.video, mode);
  }

  async getPosterFrame(): Promise<string> {
    const vid = this.video;
    if (!vid) return "";

    return new Promise<string>((resolve) => {
      const seeked = () => {
        const canvas = document.createElement("canvas");
        canvas.width = vid.videoWidth || 320;
        canvas.height = vid.videoHeight || 180;
        canvas.getContext("2d")?.drawImage(vid, 0, 0, canvas.width, canvas.height);
        resolve(canvas.toDataURL("image/webp", 0.8));
      };
      if (vid.readyState >= 2) {
        seeked();
      } else {
        vid.addEventListener("loadeddata", seeked, { once: true });
      }
    });
  }

  isAnimated(): boolean {
    return true;
  }

  private applyFit(el: HTMLVideoElement, mode: FitMode): void {
    el.style.objectFit = FIT_OBJECT[mode];
    el.style.objectPosition = "center";
  }
}
