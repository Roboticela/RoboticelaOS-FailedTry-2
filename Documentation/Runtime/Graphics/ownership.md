# Ownership & Lifetime (RoboGraphics 6.1)

| Object | Ownership | Notes |
|---|---|---|
| `GfxFramebuffer` | retain/release | OWNED frees RoboMem pixels; BORROWED never frees pixels |
| `GfxSurface` | retain/release | Retains an internal framebuffer; OWNED/BORROWED match that FB |
| `GfxImage` | retain/release | Raster images retain their surface |
| `GfxFrame` | value struct | Borrows `target` surface; does not retain |
| Geometry / color / format info | value / static | No heap |
| Backend info | static table | No heap |

## Rules

* Last `framebuffer_release` frees owned pixels via RoboMem.
* Last `surface_release` releases the retained framebuffer (which may free owned pixels).
* Borrowed framebuffers/surfaces require the external buffer to outlive the object.
* Surface map pointers must not be used after `unmap` or final surface release.
* Images that wrap a surface keep it alive until the image is released.
* Frames must not outlive the surface they point at unless the caller retains it.
* No separate graphics allocator — use RoboMem.

## Mutability

* Surface pixels are mutable under the declared access mode.
* Format descriptors and backend tables are immutable.
* Prefer transferring ownership of surfaces between threads later via explicit
  handoff; do not share mutable pixels without synchronization.
