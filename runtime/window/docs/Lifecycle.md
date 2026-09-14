# RoboWindow Lifecycle

See [architecture.md](architecture.md) for the full state machine.

```text
uninitialized
      ↓  create
   created  ←→  show / hide (visibility flags)
      ↓  request_close
   closing
      ↓  destroy
   closed
```

`destroy` from `created` is also valid (skip explicit close request).
