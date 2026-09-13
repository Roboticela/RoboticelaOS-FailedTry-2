# RoboContainers

Phase 3 of the Roboticela Runtime. Containers depend on **RoboBase** and
**RoboMem** only.

Source: `Include/RoboContainers/`, `Source/RoboContainers/`.

## RoboArray (Goal 3.1)

Dynamic contiguous array (vector-like).

### Purpose

Ordered, indexable, growable sequences used throughout the runtime later for:

- DOM child lists (`sizeof(void *)` elements)
- CSS rule / selector lists
- Layout / paint command lists
- Event queues (as arrays before a dedicated queue)
- Network / byte buffers (`element_size == 1`)

### Element representation

**Fixed-size value elements.** At `robo_array_init(array, element_size)`, every
slot is `element_size` bytes. Push/set **copy** that many bytes (`memcpy`).

| Use case | `element_size` |
|---|---|
| Pointers to heap objects | `sizeof(void *)` |
| POD structs (commands, rules) | `sizeof(MyStruct)` |
| Byte buffer | `1` |

The array does **not** deep-own pointed-to objects. If you store pointers, you
own the pointees separately.

### Ownership / lifetime

- Caller owns the `robo_array_t` (usually stack-allocated).
- Array owns the contiguous `data` buffer (via RoboMem) until `destroy`.
- Element pointers from `get` / `data` are invalidated by `push`, `reserve`,
  `destroy`, or any reallocation.
- `clear` keeps capacity; does not free the buffer.
- `destroy` frees the buffer and zeroes fields; call `init` again to reuse.

### Size vs capacity

| | |
|---|---|
| **size** | Live element count |
| **capacity** | Allocated element slots (≥ size) |

### Growth strategy

On `push` when `size == capacity`:

1. Target at least `size + 1`, and at least **8** on first growth.
2. Otherwise **double** capacity until large enough (overflow-safe).
3. If doubling would overflow `size_t`, try exact `needed` capacity.
4. Byte length = `capacity * element_size` with overflow checks →
   `ROBO_OUT_OF_MEMORY` if unsatisfiable.

`reserve(n)` grows to at least `n` slots; never shrinks.

### Memory behavior

Uses `robo_realloc` / `robo_free` only (no direct libc allocator).

### Failure behavior

| Result | When |
|---|---|
| `ROBO_OK` | Success |
| `ROBO_INVALID_ARGUMENT` | NULL array/element, `element_size == 0`, OOB set |
| `ROBO_OUT_OF_MEMORY` | Alloc/realloc failure or size overflow |
| `ROBO_NOT_FOUND` | `pop` on empty array |

OOB `get` returns **NULL** (no abort).

### Thread-safety

**Not thread-safe.** No internal locks.

### API summary

```text
robo_array_init / destroy
robo_array_size / capacity / element_size / empty
robo_array_data / data_const
robo_array_get / get_const / set
robo_array_push / pop / clear / reserve
```

Header: `Include/RoboContainers/RoboArray.h`

### Not yet (later goals for arrays)

- Small-buffer optimization

---

## RoboString (Goal 3.2)

Dynamic UTF-8 **byte** string.

### Purpose

Foundational text type for identifiers, attribute values, CSS text, logs, URLs,
and other runtime strings before full Unicode/I18N layers exist.

### UTF-8 representation

- Intended encoding: **UTF-8**.
- Storage: **opaque bytes** — RoboString does **not** validate UTF-8 on
  set/append.
- Invalid UTF-8 sequences are accepted and preserved as-is.
- `robo_string_size()` is **byte length**, not Unicode character / grapheme
  count. Multibyte characters occupy multiple bytes.

### Null termination

After a successful `init` / `from_cstr`, `data()` is always a C string pointer
with a trailing `'\0'` at `data[size]`.

Embedded `'\0'` bytes **are allowed** via `set` / `append` with an explicit
length. In that case:

- `size` includes those bytes.
- `data()` remains NUL-terminated **after** the full `size` bytes.
- Do not use `strlen(data())` when embedded NULs may be present; use `size()`.

### Ownership / lifetime

- Caller owns the `robo_string_t` object.
- String owns its heap buffer (RoboMem) until `destroy`.
- `data()` is invalidated by reallocating operations (`set`, `append`,
  `reserve`, `destroy`).
- `clear` keeps capacity.
- `from_cstr` initializes a fresh string — do not call it on an already-live
  string without `destroy` first (would leak).

### Capacity / growth

- `capacity`: content bytes available **excluding** the trailing NUL.
- Allocated buffer size is always `capacity + 1`.
- Growth: geometric ×2 from a minimum of **16** content bytes (overflow-safe),
  same spirit as RoboArray.

### C-string helpers — NULL policy

| API | NULL `cstr` |
|---|---|
| `from_cstr` | Empty string |
| `set_cstr` | Clear to empty |
| `append_cstr` | No-op success |

NULL `str` → `ROBO_INVALID_ARGUMENT` (or empty/`NULL` for queries).

### Failure behavior

| Result | When |
|---|---|
| `ROBO_OK` | Success |
| `ROBO_INVALID_ARGUMENT` | NULL str / non-NULL length with NULL bytes |
| `ROBO_OUT_OF_MEMORY` | Alloc failure or size overflow |

### Thread-safety

**Not thread-safe.**

### API summary

```text
robo_string_init / destroy / from_cstr
robo_string_size / capacity / empty / data
robo_string_clear / reserve
robo_string_set / set_cstr
robo_string_append / append_cstr / append_char
robo_string_compare
```

Header: `Include/RoboContainers/RoboString.h`

### Not yet (later goals)

- HashMap, Set, Queue, Stack, StringBuilder
- Unicode normalization / grapheme / validation APIs (see `unicode/README.md` —
  encoding lives in RoboUnicode; RoboString adds thin wrappers)
- Regex / text shaping
- Intrusive lists
- Small-buffer optimization

### RoboString + Unicode

`robo_string_size` remains **byte** length. Use
`robo_string_codepoint_count` / `robo_string_next_codepoint` /
`robo_string_utf8_valid` for scalar-level views. Grapheme / display width are
not provided yet.