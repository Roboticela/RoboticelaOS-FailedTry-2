# RoboWindow Threading (Goals 7.1 / 7.2)

Owner-thread model without OS threads yet:

1. `WindowThreadBindOwner(tid)` — designate the window-owner token
2. `WindowThreadEnter(tid)` — set simulated current thread
3. Mutating APIs call `WindowThreadRequireOwner`

* Unbound owner (`RoboWindowThreadNone`) → no enforcement (default)
* Wrong current thread → `RoboPermissionDenied`
* Backend init/shutdown run on the caller’s thread during register/clear
* Event push/poll are not internally locked
* Present is synchronous at the API boundary

No cross-thread dispatcher in Goal 7.2.
