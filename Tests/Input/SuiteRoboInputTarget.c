/*
 * RoboInput target suite (Phase 12.1) — `input.target.*`.
 *
 * A target is *identity*, and the case that makes that concrete is `fromHitTest`: the three flags a target
 * carries are the three flags a paint hit test concludes (`RoboPaintHitTest.h`), written down in this module
 * so that a dispatcher may copy the word unchanged. The suite includes the paint header **only to compare the
 * constants**, which is the one way a mirrored constant is allowed to exist in this tree: if paint renumbers
 * its flags, this case fails here rather than a dispatcher silently misreading them later.
 *
 * The rest is the kind discipline: which fields a kind carries, which it must *not* carry, and the four
 * records a producer actually gets wrong (a document tip with no window, a window kind with no id, a type
 * that is neither, and a plane that is unset).
 */

#include "RoboInputTestSupport.h"

/* The mirrored constants' origin. This is the *only* reason this suite can see RoboPaint: the boundary scan
 * keeps the module itself well below it, and a test that proves a mirror is not the module depending on it. */
#include "RoboPaint/RoboPaintHitTest.h"

/* ------------------------------------------------------------------ */
/* Kinds                                                               */
/* ------------------------------------------------------------------ */

static void TestTargetKinds(void)
{
    InputTarget target;
    InputTarget other;
    HtmlNodeHandle handle = { 9u, 12u, 2u };
    InputDiag diag;

    InputTargetInit(&target);
    RoboCheckEqInt(target.kind, RoboInputTargetNone);
    RoboCheckEqInt(target.window, RoboWindowIdNone);
    RoboCheckEqInt(target.application, RoboInputApplicationIdNone);
    RoboCheckEqInt(target.space, RoboInputSpaceUnknown);
    RoboCheckEqInt(target.flags, 0u);
    RoboCheckEqInt(target.layoutNode, RoboLayoutNodeIdNone);
    RoboCheck(!HtmlNodeHandleIsWellformed(&target.domNode));
    InputTargetInit(NULL);

    /* A cleared target has no plane, and a target without a plane is refused: a target whose plane is unset
     * could not be compared against the event it belongs to, which is the check that makes it trustworthy. */
    RoboCheck(!InputTargetIsValid(&target));
    RoboCheck(!InputTargetIsValid(NULL));
    RoboCheckEqInt(InputTargetSetNone(&target, RoboInputSpaceUnknown, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(diag.domain, RoboInputDomainTarget);
    RoboCheckEqInt(InputTargetSetNone(&target, RoboInputSpaceScreen, NULL), RoboOk);
    RoboCheck(InputTargetIsValid(&target));
    RoboCheckEqInt(target.kind, RoboInputTargetNone);
    RoboCheckEqInt(target.space, RoboInputSpaceScreen);
    RoboCheck(!InputTargetHasDocument(&target));
    RoboCheck(!InputTargetHasDocument(NULL));

    /* A window target: a window, and nothing inside it. */
    RoboCheckEqInt(InputTargetSetWindow(&target, 0u, RoboInputSpaceScreen, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(diag.domain, RoboInputDomainTarget);
    RoboCheckEqInt(InputTargetSetWindow(&target, 3u, RoboInputSpaceScreen, NULL), RoboOk);
    RoboCheckEqInt(target.kind, RoboInputTargetWindow);
    RoboCheck(InputTargetIsValid(&target));
    RoboCheckEqInt(InputTargetIsWindow(&target, 3u), 1);
    RoboCheckEqInt(InputTargetIsWindow(&target, 4u), 0);
    RoboCheckEqInt(InputTargetIsWindow(NULL, 3u), 0);

    /* An application target: an application, and no window of it yet. */
    RoboCheckEqInt(InputTargetSetApplication(&target, 0u, RoboInputSpaceWindow, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(InputTargetSetApplication(&target, 5u, RoboInputSpaceWindow, NULL),
                      RoboOk);
    RoboCheckEqInt(target.kind, RoboInputTargetApplication);
    RoboCheckEqInt(target.window, RoboWindowIdNone);
    RoboCheck(InputTargetIsValid(&target));

    /* A document target: a window **and** the node under the point. */
    RoboCheckEqInt(InputTargetSetDocument(&target, 0u, &handle, 7u, 0u,
                                                    RoboInputSpaceDocument, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(diag.domain, RoboInputDomainTarget);
    RoboCheckEqInt(InputTargetSetDocument(&target, 3u, NULL, RoboLayoutNodeIdNone, 0u,
                                                    RoboInputSpaceDocument, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(InputTargetSetDocument(&target, 3u, &handle, 7u,
                                                    RoboInputTargetFlagAll | (uint32_t)(1u << 20),
                                                    RoboInputSpaceDocument, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(diag.domain, RoboInputDomainTarget);

    /* Either identity alone is enough: a handle with no layout id, and a layout id with no handle. */
    RoboCheckEqInt(InputTargetSetDocument(&target, 3u, &handle, RoboLayoutNodeIdNone,
                                                    RoboInputTargetFlagText,
                                                    RoboInputSpaceDocument, NULL),
                      RoboOk);
    RoboCheckEqInt(target.kind, RoboInputTargetDocument);
    RoboCheck(InputTargetHasDocument(&target));
    RoboCheck(InputTargetIsValid(&target));
    RoboCheck(InputTargetHasFlag(&target, RoboInputTargetFlagText));
    RoboCheck(!InputTargetHasFlag(&target, RoboInputTargetFlagContent));
    RoboCheck(!InputTargetHasFlag(NULL, RoboInputTargetFlagText));
    {
        HtmlNodeHandle outHandle;
        LayoutNodeId outNode = RoboLayoutNodeIdNone;

        RoboCheckEqInt(InputTargetDocument(&target, &outHandle, &outNode), 1);
        RoboCheckEqInt(outHandle.documentId, 9u);
        RoboCheckEqInt(outHandle.nodeId, 12u);
        RoboCheckEqInt(outHandle.generation, 2u);
        RoboCheckEqInt(outNode, RoboLayoutNodeIdNone);
    }

    RoboCheckEqInt(InputTargetSetDocument(&target, 3u, NULL, 41u, 0u, RoboInputSpaceDocument,
                                                    NULL),
                      RoboOk);
    RoboCheck(InputTargetIsValid(&target));
    RoboCheckEqInt(target.layoutNode, 41u);

    /* The document identity read back is all-or-nothing: a target of another kind answers "no document" and
     * clears what it was given. */
    RoboCheckEqInt(InputTargetSetWindow(&target, 3u, RoboInputSpaceScreen, NULL), RoboOk);
    {
        HtmlNodeHandle outHandle = { 1u, 1u, 1u };
        LayoutNodeId outNode = 9u;

        RoboCheckEqInt(InputTargetDocument(&target, &outHandle, &outNode), 0);
        RoboCheck(!HtmlNodeHandleIsWellformed(&outHandle));
        RoboCheckEqInt(outNode, RoboLayoutNodeIdNone);
        RoboCheckEqInt(InputTargetDocument(&target, NULL, NULL), 0);
    }

    /* The four records a producer gets wrong, each refused by the *validator* rather than only by a setter --
     * a target can arrive from anywhere. */
    {
        InputTarget broken;

        /* A document kind with no window. */
        InputTargetInit(&broken);
        broken.kind = RoboInputTargetDocument;
        broken.space = RoboInputSpaceDocument;
        broken.layoutNode = 3u;
        RoboCheck(!InputTargetIsValid(&broken));

        /* ... with a window but no node at all: a window target wearing a document's kind. */
        broken.window = 3u;
        broken.layoutNode = RoboLayoutNodeIdNone;
        RoboCheck(!InputTargetIsValid(&broken));

        /* A window kind with everything a window target must not carry. */
        InputTargetInit(&broken);
        broken.kind = RoboInputTargetWindow;
        broken.space = RoboInputSpaceScreen;
        broken.window = 3u;
        broken.application = 4u;
        RoboCheck(!InputTargetIsValid(&broken));
        broken.application = RoboInputApplicationIdNone;
        broken.flags = (uint32_t)RoboInputTargetFlagText;
        RoboCheck(!InputTargetIsValid(&broken));
        broken.flags = 0u;
        broken.layoutNode = 8u;
        RoboCheck(!InputTargetIsValid(&broken));
        broken.layoutNode = RoboLayoutNodeIdNone;
        RoboCheck(InputTargetIsValid(&broken));

        /* An application kind that also names a window, and a kind that is not one. */
        broken.kind = RoboInputTargetApplication;
        broken.application = 4u;
        RoboCheck(!InputTargetIsValid(&broken));
        broken.window = RoboWindowIdNone;
        RoboCheck(InputTargetIsValid(&broken));
        broken.kind = (InputTargetKind)9;
        RoboCheck(!InputTargetIsValid(&broken));

        /* A plane that is not one. */
        InputTargetInit(&broken);
        broken.space = (InputSpace)9;
        RoboCheck(!InputTargetIsValid(&broken));
    }

    /* Equality is field-by-field, and the two records are not the same one. */
    RoboCheckEqInt(InputTargetSetWindow(&target, 3u, RoboInputSpaceScreen, NULL), RoboOk);
    other = target;
    RoboCheck(InputTargetEqual(&target, &other));
    RoboCheck(InputTargetEqual(NULL, NULL));
    RoboCheck(!InputTargetEqual(&target, NULL));
    other.window = 4u;
    RoboCheck(!InputTargetEqual(&target, &other));
    other = target;
    other.space = RoboInputSpaceWindow;
    RoboCheck(!InputTargetEqual(&target, &other));

    /* Setting a target through a door resets a record that had something else in it, so a caller that reuses
     * one record per event cannot leave a stale field behind. */
    RoboCheckEqInt(InputTargetSetDocument(&target, 3u, &handle, 12u, 0u,
                                                    RoboInputSpaceDocument, NULL),
                      RoboOk);
    RoboCheckEqInt(InputTargetSetWindow(&target, 6u, RoboInputSpaceScreen, NULL), RoboOk);
    RoboCheck(!HtmlNodeHandleIsWellformed(&target.domNode));
    RoboCheckEqInt(target.layoutNode, RoboLayoutNodeIdNone);
    RoboCheckEqInt(target.flags, 0u);
    RoboCheckEqInt(target.space, RoboInputSpaceScreen);
}

/* ------------------------------------------------------------------ */
/* The mirrored hit-test facts                                         */
/* ------------------------------------------------------------------ */

static void TestTargetFromHitTest(void)
{
    /* The three facts a hit test concludes, mirrored with the same bit positions. Asserted *equal* to
     * paint's, which is what makes the mirror a checked copy rather than a second vocabulary. */
    RoboCheckEqInt(RoboInputTargetFlagText, RoboPaintHitFlagText);
    RoboCheckEqInt(RoboInputTargetFlagContent, RoboPaintHitFlagContent);
    RoboCheckEqInt(RoboInputTargetFlagTransparent, RoboPaintHitFlagTransparent);

    /* A word a hit test produced can therefore be carried unchanged, including "all three". */
    {
        InputTarget target;
        HtmlNodeHandle handle = { 2u, 3u, 1u };
        uint32_t fromHitTest = RoboPaintHitFlagText | RoboPaintHitFlagContent |
                                 RoboPaintHitFlagTransparent;

        RoboCheckEqInt(InputTargetSetDocument(&target, 1u, &handle, 5u, fromHitTest,
                                                        RoboInputSpaceDocument, NULL),
                          RoboOk);
        RoboCheck(InputTargetHasFlag(&target, RoboInputTargetFlagText));
        RoboCheck(InputTargetHasFlag(&target, RoboInputTargetFlagContent));
        RoboCheck(InputTargetHasFlag(&target, RoboInputTargetFlagTransparent));
        RoboCheck(InputTargetIsValid(&target));
    }

    /* The integration contract, asserted end to end without a dispatcher existing: an input event carries
     * the window a hit test was performed in, the document plane a hit test reads, and the identity a hit
     * test answered with -- so the next phase can route it and this phase has not routed anything. */
    {
        InputEvent event = InputPointerMove(120, 84, 0u, RoboInputSpaceDocument, 3u);
        InputTarget target;
        HtmlNodeHandle handle = { 4u, 17u, 3u };

        RoboCheckEqInt(InputTargetSetDocument(&target, event.window, &handle, 41u,
                                                        RoboInputTargetFlagContent,
                                                        event.space, NULL),
                          RoboOk);
        RoboCheckEqInt(InputEventSetTarget(&event, &target, NULL), RoboOk);
        RoboCheck(InputEventIsValid(&event));
        RoboCheckEqInt(InputTargetIsWindow(&event.target, event.window), 1);
        RoboCheck(InputTargetHasDocument(&event.target));
    }
}

void TestRegisterInputTarget(void)
{
    TestRegister("input.target.kinds", TestTargetKinds);
    TestRegister("input.target.from_hit_test", TestTargetFromHitTest);
}
