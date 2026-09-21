# RoboWindow Backend (Goals 7.1 / 7.2)

Single active backend via `WindowBackendRegister` /
`WindowBackendUseNull`.

## Null backend

In-process lifecycle only:

* Empty native handle after create
* `present` / `focus` → `RoboNotSupported`
* Does not create OS windows

## Mock backends

Test suites register mock vtables (see `SuiteRoboWindow*.c`). Mocks may assign
valid handles and report actual geometry without claiming OS windows exist.

## Errors

| Situation | Result |
|---|---|
| No backend | `RoboNotSupported` |
| Missing required vtbl field | `RoboInvalidArgument` |
| Backend init failure | Propagated (`RoboInitError`, …) |
| Capability false | `RoboNotSupported` |
| Invalid window id | `RoboNotFound` |
| Invalid UTF-8 title | `RoboParseError` |
| Wrong owner thread | `RoboPermissionDenied` |
