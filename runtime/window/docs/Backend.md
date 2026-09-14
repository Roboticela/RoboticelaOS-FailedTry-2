# RoboWindow Backend (Goals 7.1 / 7.2)

Single active backend via `robo_window_backend_register` /
`robo_window_backend_use_null`.

## Null backend

In-process lifecycle only:

* Empty native handle after create
* `present` / `focus` → `ROBO_NOT_SUPPORTED`
* Does not create OS windows

## Mock backends

Test suites register mock vtables (see `SuiteRoboWindow*.c`). Mocks may assign
valid handles and report actual geometry without claiming OS windows exist.

## Errors

| Situation | Result |
|---|---|
| No backend | `ROBO_NOT_SUPPORTED` |
| Missing required vtbl field | `ROBO_INVALID_ARGUMENT` |
| Backend init failure | Propagated (`ROBO_INIT_ERROR`, …) |
| Capability false | `ROBO_NOT_SUPPORTED` |
| Invalid window id | `ROBO_NOT_FOUND` |
| Invalid UTF-8 title | `ROBO_PARSE_ERROR` |
| Wrong owner thread | `ROBO_PERMISSION_DENIED` |
