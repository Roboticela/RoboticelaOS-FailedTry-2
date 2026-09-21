# User-space heap

`malloc` / `calloc` / `realloc` / `free` use a first-fit allocator on
anonymous `SYS_MMAP` pages. They never call the kernel heap (`kmalloc`).

Semantics:

* `malloc(0)` returns a unique (or reusable) non-NULL block when memory
  exists
* `free(NULL)` is a no-op
* `realloc(NULL, n)` is `malloc(n)`
* `realloc(p, 0)` frees `p` and returns NULL
* `calloc` checks `count * size` overflow
* failure sets `errno` to `ENOMEM` and leaves the original block intact
  on `realloc`

Alignment is 16 bytes. Large requests map additional 64 KiB (or larger)
arenas. Adjacent free blocks are coalesced.

There is no `sbrk`.
