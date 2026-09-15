# Backend Capabilities (Goals 7.2 / 7.4)

```c
robo_window_backend_caps_t
```

| Flag | Meaning |
|---|---|
| `create_window` / `destroy_window` | Native create/destroy |
| `show_hide` | Show / hide |
| `resize` / `move` | Size / position requests |
| `size_constraints` | Runtime min/max via `set_size_constraints` |
| `set_title` | UTF-8 title |
| `focus` | Focus request |
| `query_state` | Backend may fill actual state |
| `present` | Framebuffer / surface presentation |
| `process_events` | Native event drain |
| `reports_actual_state` | Do not assume request == actual |
| `present_format_mask` | `ROBO_WINDOW_PRESENT_FMT_*` bits (when present) |
| `present_max_width` / `present_max_height` | Optional ceilings; 0 = unlimited |

Query via `robo_window_backend_capabilities`. No backend → `ROBO_NOT_SUPPORTED`
with zeroed caps.

Null architecture backend: lifecycle + title/size/move; no focus/present;
`reports_actual_state = 0`.
