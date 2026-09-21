# Threading (RoboGraphics 6.1)

Designed for eventual UI / raster / compositor / GPU threads. Goal 6.1 does
**not** implement multi-threaded raster.

| Object / API | Classification |
|---|---|
| Geometry / color / format pure functions | **thread-safe** |
| Capability / backend queries (static data) | **thread-safe** |
| Framebuffer create/release | **thread-compatible** (refcount not atomic) |
| Framebuffer pixel mutation | **thread-confined** to owner context |
| Surface create/release | **thread-compatible** (refcount not atomic) |
| Surface map/unmap | **thread-confined** |
| Surface pixel mutation | **thread-confined** to owner context |
| Image retain/release | **thread-compatible** |
| Frame begin/end | **thread-confined** with its surface |

## Guidance

* Treat a writable framebuffer/surface as owned by one execution context at a time.
* Immutable snapshots (future) will be preferred for cross-thread sharing.
* Presentation backends will document transfer rules when introduced.
