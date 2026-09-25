/*
 * RoboJS (Phase 16) — the front door.
 *
 * There is one JavaScript runtime in RoboticelaOS and this is it: a source-to-callback pipeline built
 * independently of any host JavaScript engine, exercised by 16.15's suites and consumed by the
 * document/shell layers through the two seams in the second tier below.
 *
 *   JavaScript source
 *        ↓  RoboJSLexer.h        tokens
 *        ↓  RoboJSParser.h       a tree
 *        ↓  RoboJSAst.h          the node shape and its printer
 *        ↓  RoboJSInterpreter.h  the evaluator
 *   JsValue / JsObject / JsFunction / JsEnv
 *        ↓  RoboJSDom.h          the DOM a script sees (host interface)
 *        ↓  RoboJSEvent.h        listeners, the event object, the input path
 *        ↓  RoboJSTimer.h        setTimeout and friends
 *        ↓  RoboJSEventLoop.h    tasks and microtasks
 *   the host
 *
 * **The two seams a document or shell uses.** `JsRuntimeCreate`/`JsRuntimeEvaluate`/
 * `JsRuntimeDestroy` is the lifecycle, and `JsDomInstall` with a `JsDomHost` is how a real DOM is
 * attached. `RoboJSDomHtml.h` is the shipped adapter for this tree's HTML DOM, and it is the only
 * header in this module that includes a RoboHTML one.
 *
 * **Nothing above this module is required by it.** RoboJS includes `RoboBase` and the standard C
 * library, and nothing else: no RoboHTML header outside the adapter, no RoboPaint, no RoboRender, no
 * RoboDesktop. A script reaches the outside world only through a binding this tree installed, which
 * is what `Documentation/RoboJS/Security.md` states and the security suite checks.
 *
 * The one header *not* included here is `RoboJSDomHtml.h` — including it would make every RoboJS
 * consumer depend on RoboHTML, so a document integration includes both, by name.
 */

#ifndef ROBO_JS_H
#define ROBO_JS_H

#include "RoboJS/RoboJSAst.h"
#include "RoboJS/RoboJSDom.h"
#include "RoboJS/RoboJSEnvironment.h"
#include "RoboJS/RoboJSEvent.h"
#include "RoboJS/RoboJSEventLoop.h"
#include "RoboJS/RoboJSException.h"
#include "RoboJS/RoboJSFunction.h"
#include "RoboJS/RoboJSHeap.h"
#include "RoboJS/RoboJSInterpreter.h"
#include "RoboJS/RoboJSLexer.h"
#include "RoboJS/RoboJSObject.h"
#include "RoboJS/RoboJSParser.h"
#include "RoboJS/RoboJSRuntime.h"
#include "RoboJS/RoboJSTimer.h"
#include "RoboJS/RoboJSTypes.h"
#include "RoboJS/RoboJSValue.h"
#include "RoboJS/RoboJSVersion.h"

/**
 * Module version and capability table, re-exported here so a consumer that includes only this header
 * can still ask what it is holding. `JsGetCapabilities` answers the second block too — the absences
 * — because "can this build do `class`?" is a question a caller should be able to ask and get `0`
 * from rather than discover at runtime.
 */
const char *JsVersion(void);

/** Numeric components; NULL @p out → `RoboInvalidArgument`. `JsGetVersion` without the include. */
Result JsVersionComponents(I32 *outMajor, I32 *outMinor, I32 *outPatch);

#endif /* ROBO_JS_H */
