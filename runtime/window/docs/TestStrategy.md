# RoboWindow Tests

| Filter | Suite | Goal |
|---|---|---|
| `window.` (core) | `SuiteRoboWindow.c` | 7.1 |
| `window.native.*` | `SuiteRoboWindowNative.c` | 7.2 |
| `window.present.*` | `SuiteRoboWindowPresent.c` | 7.4 |

Headless only — mock or null backend. Present suite copies pixels for integrity
checks; it does not open a host window.
