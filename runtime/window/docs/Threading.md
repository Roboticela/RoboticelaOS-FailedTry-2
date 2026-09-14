# RoboWindow Threading (Goals 7.1 / 7.2)

Owner-thread model without OS threads yet:

1. `robo_window_thread_bind_owner(tid)` — designate the window-owner token
2. `robo_window_thread_enter(tid)` — set simulated current thread
3. Mutating APIs call `robo_window_thread_require_owner`

* Unbound owner (`ROBO_WINDOW_THREAD_NONE`) → no enforcement (default)
* Wrong current thread → `ROBO_PERMISSION_DENIED`
* Backend init/shutdown run on the caller’s thread during register/clear
* Event push/poll are not internally locked
* Present is synchronous at the API boundary

No cross-thread dispatcher in Goal 7.2.
