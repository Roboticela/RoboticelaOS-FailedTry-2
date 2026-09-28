/// <reference types="vite/client" />

// Video asset imports — Vite resolves these to hashed URL strings.
declare module "*.mp4" {
  const src: string;
  export default src;
}
declare module "*.webm" {
  const src: string;
  export default src;
}
declare module "*.mov" {
  const src: string;
  export default src;
}
