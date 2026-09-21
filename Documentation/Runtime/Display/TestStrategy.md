# RoboDisplay test strategy (Goal 7.3)

Filter prefix: `display.`

| Area | Coverage |
|------|----------|
| Identity | NONE, equality, known/unknown lookup |
| Geometry | Origin 0, negative multi-monitor, work area, resolution |
| Refresh | Integer, fractional, invalid |
| Scale/DPI | 1× / 2×, logical vs physical helpers |
| Orientation | Known set + invalid |
| Primary | None / multi / non-zero ID |
| Enumeration | 0 / 1 / N / backend failure |
| Lifecycle | Init/shutdown, null/placeholder, missing backend |
| Capabilities | Modes supported vs `RoboNotSupported` |
| Window | Association set/get/clear/invalid |

Regression: run `window.*` and `graphics.*` after display changes.

Mock backend lives only in the display test suite.
