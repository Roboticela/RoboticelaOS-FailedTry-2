/*
 * RoboInput cursor suite (Phase 13.43) — `input.cursor.*`.
 *
 * Five cases about one owner and one table:
 *
 *   `vocabulary`   the twelve shapes, asserted positionally with their names, plus the flag that separates
 *                  "hide it" from "draw the arrow" -- a cleared record's value is a shape a shell can draw,
 *                  which is why `NONE` needed a number of its own;
 *
 *   `defaults`     `InputCursorForTarget`: a document **text** node is an I-beam, a document node that is
 *                  not text is the arrow, a window and an application are the arrow, and *nothing at all*
 *                  is still the arrow, because a cursor is always somewhere on a screen;
 *
 *   `override`     the caller's declaration: it beats the table, a second declaration for one target
 *                  **replaces** the first rather than queueing behind it, removal puts the table back, a
 *                  full table refuses with its counter, and a bad target or shape is refused by name;
 *
 *   `update`       the decision a pointer move makes -- the accepted/unchanged split that tells a caller
 *                  whether the shell actually redrew the cursor, and the fact that moving onto nothing
 *                  changes the shape *and* clears the source it came from;
 *
 *   `dump`         the line and its refusal when the buffer is one byte short.
 */

#include "RoboInputTestSupport.h"

#include <string.h>

static InputTarget CursorWindowTarget(WindowId window)
{
    InputTarget target;
    InputDiag diag;

    InputTargetInit(&target);
    if (InputTargetSetWindow(&target, window, RoboInputSpaceWindow, &diag) != RoboOk) {
        RoboCheckMsg(0, "the fixture's window target was refused");
    }
    return target;
}

static InputTarget CursorNodeTarget(WindowId window, LayoutNodeId node, uint32_t flags)
{
    InputTarget target;
    InputDiag diag;

    InputTargetInit(&target);
    if (InputTargetSetDocument(&target, window, NULL, node, flags, RoboInputSpaceWindow, &diag) != RoboOk) {
        RoboCheckMsg(0, "the fixture's document target was refused");
    }
    return target;
}

/* ------------------------------------------------------------------ */
/* Vocabulary                                                          */
/* ------------------------------------------------------------------ */

static void TestCursorVocabulary(void)
{
    /* Positionally, because these are the numbers a shell's cursor table is indexed by. */
    RoboCheckEqInt((int)RoboInputCursorDefault, 0);
    RoboCheckEqInt((int)RoboInputCursorPointer, 1);
    RoboCheckEqInt((int)RoboInputCursorText, 2);
    RoboCheckEqInt((int)RoboInputCursorCrosshair, 3);
    RoboCheckEqInt((int)RoboInputCursorMove, 4);
    RoboCheckEqInt((int)RoboInputCursorResizeHorizontal, 5);
    RoboCheckEqInt((int)RoboInputCursorResizeVertical, 6);
    RoboCheckEqInt((int)RoboInputCursorResizeDiagonal, 7);
    RoboCheckEqInt((int)RoboInputCursorNotAllowed, 8);
    RoboCheckEqInt((int)RoboInputCursorWait, 9);
    RoboCheckEqInt((int)RoboInputCursorProgress, 10);
    RoboCheckEqInt((int)RoboInputCursorNone, 11);
    RoboCheckEqInt((int)RoboInputCursorGrabbing, 12);
    RoboCheckEqInt((int)RoboInputCursorTypeCount, 13);

    RoboCheckStrEq(InputCursorTypeName(RoboInputCursorDefault), "default");
    RoboCheckStrEq(InputCursorTypeName(RoboInputCursorPointer), "pointer");
    RoboCheckStrEq(InputCursorTypeName(RoboInputCursorText), "text");
    RoboCheckStrEq(InputCursorTypeName(RoboInputCursorCrosshair), "crosshair");
    RoboCheckStrEq(InputCursorTypeName(RoboInputCursorMove), "move");
    RoboCheckStrEq(InputCursorTypeName(RoboInputCursorResizeHorizontal), "resize-h");
    RoboCheckStrEq(InputCursorTypeName(RoboInputCursorResizeVertical), "resize-v");
    RoboCheckStrEq(InputCursorTypeName(RoboInputCursorResizeDiagonal), "resize-diagonal");
    RoboCheckStrEq(InputCursorTypeName(RoboInputCursorNotAllowed), "not-allowed");
    RoboCheckStrEq(InputCursorTypeName(RoboInputCursorWait), "wait");
    RoboCheckStrEq(InputCursorTypeName(RoboInputCursorProgress), "progress");
    RoboCheckStrEq(InputCursorTypeName(RoboInputCursorNone), "none");
    RoboCheckStrEq(InputCursorTypeName(RoboInputCursorGrabbing), "grabbing");

    RoboCheckStrEq(InputCursorTypeName((InputCursorType)13), "unknown");
    RoboCheckStrEq(InputCursorTypeName((InputCursorType)-1), "unknown");
    RoboCheck(InputCursorTypeIsKnown(RoboInputCursorDefault));
    RoboCheck(InputCursorTypeIsKnown(RoboInputCursorGrabbing));
    RoboCheck(!InputCursorTypeIsKnown((InputCursorType)13));

    /* Only one shape means "there is no cursor", and it is not the default. */
    RoboCheck(InputCursorTypeIsHidden(RoboInputCursorNone));
    RoboCheck(!InputCursorTypeIsHidden(RoboInputCursorDefault));
    RoboCheck(!InputCursorTypeIsHidden(RoboInputCursorWait));
}

/* ------------------------------------------------------------------ */
/* The default table                                                   */
/* ------------------------------------------------------------------ */

static void TestCursorDefaults(void)
{
    const WindowId window = (WindowId)7u;
    InputTarget text = CursorNodeTarget(window, (LayoutNodeId)11u, RoboInputTargetFlagText);
    InputTarget content = CursorNodeTarget(window, (LayoutNodeId)12u, RoboInputTargetFlagContent);
    InputTarget plain = CursorNodeTarget(window, (LayoutNodeId)13u, 0u);
    InputTarget windowTarget = CursorWindowTarget(window);
    InputTarget application;
    InputTarget none;
    InputDiag diag;

    RoboCheckEqInt((int)InputCursorForTarget(&text), (int)RoboInputCursorText);
    /* A content node that is not a text run: a picture, a canvas, a box. "Something graphical" has no one
     * shape, so the table answers the arrow rather than inventing one. */
    RoboCheckEqInt((int)InputCursorForTarget(&content), (int)RoboInputCursorDefault);
    RoboCheckEqInt((int)InputCursorForTarget(&plain), (int)RoboInputCursorDefault);

    /* A window's own chrome belongs to the shell: the table answers the arrow and the shell declares an
     * override where it resizes or moves. */
    RoboCheckEqInt((int)InputCursorForTarget(&windowTarget), (int)RoboInputCursorDefault);

    InputTargetInit(&application);
    RoboCheckEqInt(InputTargetSetApplication(&application, (InputApplicationId)3u, RoboInputSpaceApplication,
                                             &diag),
                   RoboOk);
    RoboCheckEqInt((int)InputCursorForTarget(&application), (int)RoboInputCursorDefault);

    /* Nothing at all -- the pointer is over a blank area of the shell, and that is still the shell. */
    InputTargetInit(&none);
    RoboCheckEqInt((int)InputCursorForTarget(&none), (int)RoboInputCursorDefault);
    RoboCheckEqInt((int)InputCursorForTarget(NULL), (int)RoboInputCursorDefault);

    /* Text plus content is still text: the flags are independent facts, not a ranking. */
    {
        InputTarget both = CursorNodeTarget(window, (LayoutNodeId)14u,
                                            RoboInputTargetFlagText | RoboInputTargetFlagContent);

        RoboCheckEqInt((int)InputCursorForTarget(&both), (int)RoboInputCursorText);
    }
}

/* ------------------------------------------------------------------ */
/* Overrides                                                           */
/* ------------------------------------------------------------------ */

static void TestCursorOverride(void)
{
    const WindowId window = (WindowId)7u;
    InputCursorManager manager;
    InputTarget windowTarget = CursorWindowTarget(window);
    InputTarget text = CursorNodeTarget(window, (LayoutNodeId)11u, RoboInputTargetFlagText);
    InputTarget other = CursorNodeTarget(window, (LayoutNodeId)12u, RoboInputTargetFlagText);
    InputDiag diag;
    uint32_t index;

    InputDiagReset(&diag);
    InputCursorManagerInit(&manager);
    RoboCheckEqInt((int)InputCursorManagerCurrent(&manager), (int)RoboInputCursorDefault);
    RoboCheckEqInt(manager.count, 0);

    /* A declaration beats the table: a window's edge declares the resize shape the table would never
     * guess. */
    RoboCheckEqInt(InputCursorManagerSetOverride(&manager, &windowTarget, RoboInputCursorResizeDiagonal,
                                                 &diag),
                   RoboOk);
    RoboCheckEqInt(manager.count, 1);
    RoboCheckEqInt((int)InputCursorForTarget(&windowTarget), (int)RoboInputCursorDefault);
    RoboCheckEqInt((int)InputCursorManagerUpdate(&manager, &windowTarget, NULL),
                   (int)RoboInputCursorResizeDiagonal);
    RoboCheckEqInt(manager.accepted, 1);

    /* A declaration for a text node that is *not* an I-beam -- a read-only field that wants the arrow. */
    RoboCheckEqInt(InputCursorManagerSetOverride(&manager, &text, RoboInputCursorWait, &diag), RoboOk);
    RoboCheckEqInt((int)InputCursorManagerUpdate(&manager, &text, NULL), (int)RoboInputCursorWait);
    RoboCheckEqInt(manager.count, 2);

    /* Declaring again **replaces**: two shapes for one target would make the answer depend on registration
     * order, which is a rule nobody could read. */
    RoboCheckEqInt(InputCursorManagerSetOverride(&manager, &text, RoboInputCursorCrosshair, &diag), RoboOk);
    RoboCheckEqInt(manager.count, 2);
    RoboCheckEqInt((int)InputCursorManagerUpdate(&manager, &text, NULL), (int)RoboInputCursorCrosshair);

    /* A shape that is not declared, and a target that is not a record this build may carry. */
    RoboCheckEqInt(InputCursorManagerSetOverride(&manager, &text, (InputCursorType)99, &diag),
                   RoboInvalidArgument);
    {
        InputTarget invalid;

        InputTargetInit(&invalid);
        RoboCheckEqInt(InputCursorManagerSetOverride(&manager, &invalid, RoboInputCursorText, &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt(InputCursorManagerSetOverride(&manager, NULL, RoboInputCursorText, &diag),
                       RoboInvalidArgument);
    }
    RoboCheckEqInt(manager.refused, 3);
    RoboCheckEqInt(manager.count, 2);

    /* Removal puts the table back, and removing something never declared is not an error. */
    RoboCheck(InputCursorManagerRemoveOverride(&manager, &windowTarget));
    RoboCheckEqInt(manager.count, 1);
    RoboCheck(!InputCursorManagerRemoveOverride(&manager, &windowTarget));
    RoboCheckEqInt((int)InputCursorManagerUpdate(&manager, &windowTarget, NULL),
                   (int)RoboInputCursorDefault);

    /* A full table refuses by name rather than evicting somebody's declaration. */
    {
        InputCursorManager full;

        InputCursorManagerInit(&full);
        for (index = 0u; index < RoboInputMaxCursorOverrides; index++) {
            InputTarget slot = CursorNodeTarget(window, (LayoutNodeId)(100u + index), 0u);

            RoboCheckEqInt(InputCursorManagerSetOverride(&full, &slot, RoboInputCursorMove, &diag), RoboOk);
        }
        RoboCheckEqInt(full.count, (int)RoboInputMaxCursorOverrides);
        RoboCheckEqInt(InputCursorManagerSetOverride(&full, &other, RoboInputCursorMove, &diag),
                       RoboOutOfMemory);
        RoboCheckEqInt(full.refused, 1);

        /* Clearing takes them all at once and does not change the shape in force: the cursor on screen stays
         * where it is until the next move decides. */
        {
            InputCursorType before = InputCursorManagerCurrent(&full);

            RoboCheckEqInt((int)InputCursorManagerClearOverrides(&full), (int)RoboInputMaxCursorOverrides);
            RoboCheckEqInt(full.count, 0);
            RoboCheckEqInt((int)InputCursorManagerCurrent(&full), (int)before);
        }
    }
    RoboCheckEqInt((int)InputCursorManagerClearOverrides(NULL), 0);
}

/* ------------------------------------------------------------------ */
/* Update                                                              */
/* ------------------------------------------------------------------ */

static void TestCursorUpdate(void)
{
    const WindowId window = (WindowId)7u;
    InputCursorManager manager;
    InputTarget text = CursorNodeTarget(window, (LayoutNodeId)11u, RoboInputTargetFlagText);
    InputTarget plain = CursorNodeTarget(window, (LayoutNodeId)13u, 0u);
    InputDiag diag;
    int accepted = -1;

    InputDiagReset(&diag);
    InputCursorManagerInit(&manager);

    /* A fresh manager starts as the arrow, so moving onto a text node is a change and the shell redraws. */
    RoboCheckEqInt((int)InputCursorManagerUpdate(&manager, &text, &accepted), (int)RoboInputCursorText);
    RoboCheckEqInt(accepted, 1);
    RoboCheckEqInt(manager.accepted, 1);
    RoboCheckEqInt(manager.unchanged, 0);

    /* Staying on the same node decides the same shape: counted as unchanged, and the source stays. */
    accepted = -1;
    RoboCheckEqInt((int)InputCursorManagerUpdate(&manager, &text, &accepted), (int)RoboInputCursorText);
    RoboCheckEqInt(accepted, 0);
    RoboCheckEqInt(manager.accepted, 1);
    RoboCheckEqInt(manager.unchanged, 1);

    /* A different node with the *same* shape is still unchanged: the cursor is a shape on screen, not a
     * target identity. */
    RoboCheckEqInt((int)InputCursorManagerUpdate(&manager, &plain, &accepted), (int)RoboInputCursorDefault);
    RoboCheckEqInt(accepted, 1);
    RoboCheckEqInt(manager.accepted, 2);

    /* ... and the source now names the node that produced it. */
    RoboCheck(InputCursorManagerSource(&manager) != NULL);
    RoboCheck(InputTargetEqual(InputCursorManagerSource(&manager), &plain));

    RoboCheckEqInt((int)InputCursorManagerUpdate(&manager, &plain, &accepted), (int)RoboInputCursorDefault);
    RoboCheckEqInt(accepted, 0);
    RoboCheckEqInt(manager.unchanged, 2);

    /* The pointer left the document: the shape changes back to the arrow and there is no source left. */
    RoboCheckEqInt((int)InputCursorManagerUpdate(&manager, &text, &accepted), (int)RoboInputCursorText);
    RoboCheckEqInt((int)InputCursorManagerUpdate(&manager, NULL, &accepted), (int)RoboInputCursorDefault);
    RoboCheckEqInt(accepted, 1);
    RoboCheck(InputCursorManagerSource(&manager) == NULL);

    /* A NULL manager is the arrow with nothing accepted, because there is nobody to tell. */
    RoboCheckEqInt((int)InputCursorManagerUpdate(NULL, &text, &accepted), (int)RoboInputCursorDefault);
    RoboCheckEqInt(accepted, 0);
    RoboCheckEqInt((int)InputCursorManagerCurrent(NULL), (int)RoboInputCursorDefault);
    RoboCheck(InputCursorManagerSource(NULL) == NULL);
}

/* ------------------------------------------------------------------ */
/* Dump                                                                */
/* ------------------------------------------------------------------ */

static void TestCursorDump(void)
{
    const WindowId window = (WindowId)7u;
    InputCursorManager manager;
    InputCursorManager empty;
    InputTarget windowTarget = CursorWindowTarget(window);
    InputTarget text = CursorNodeTarget(window, (LayoutNodeId)11u, RoboInputTargetFlagText);
    InputDiag diag;
    char line[512];
    char again[512];
    size_t length = 0u;
    size_t againLength = 0u;

    InputDiagReset(&diag);
    InputCursorManagerInit(&manager);
    RoboCheckEqInt(InputCursorManagerSetOverride(&manager, &windowTarget, RoboInputCursorResizeDiagonal,
                                                 &diag),
                   RoboOk);
    (void)InputCursorManagerUpdate(&manager, &text, NULL);
    (void)InputCursorManagerUpdate(&manager, &text, NULL);

    RoboCheckEqInt(InputCursorManagerWrite(&manager, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckEqInt((int)strlen(line), (int)length);
    RoboCheckStrEq(line,
                   "cursor shape=text from=document overrides=1 accepted=1 unchanged=1 refused=0\n"
                   "  override kind=window window=7 shape=resize-diagonal\n");

    InputCursorManagerInit(&empty);
    RoboCheckEqInt(InputCursorManagerWrite(&empty, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "cursor shape=default from=none overrides=0 accepted=0 unchanged=0 refused=0\n");
    RoboCheckEqInt(InputCursorManagerWrite(&empty, again, sizeof(again), &againLength, &diag), RoboOk);
    RoboCheckEqInt((int)againLength, (int)length);
    RoboCheck(strcmp(line, again) == 0);

    RoboCheckEqInt(InputCursorManagerWrite(NULL, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "cursor <none>\n");

    /* A buffer one byte short of the first generation: refuse and leave it empty rather than a prefix the
     * reader would parse as the whole thing. */
    {
        char small[32];
        size_t needed = 0u;

        RoboCheckEqInt(InputCursorManagerWrite(&manager, small, sizeof(small), &needed, &diag),
                       RoboNotSupported);
        RoboCheckStrEq(small, "?");
    }
}

/* ------------------------------------------------------------------ */
/* Frame                                                               */
/* ------------------------------------------------------------------ */

void TestRegisterInputCursor(void)
{
    TestRegister("input.cursor.vocabulary", TestCursorVocabulary);
    TestRegister("input.cursor.defaults", TestCursorDefaults);
    TestRegister("input.cursor.override", TestCursorOverride);
    TestRegister("input.cursor.update", TestCursorUpdate);
    TestRegister("input.cursor.dump", TestCursorDump);
}
