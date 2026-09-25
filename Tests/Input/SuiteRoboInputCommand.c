/*
 * RoboInput command-layer suite (Phase 13.12/13.13/13.35/13.36) — `input.command.*` and `input.shortcut.*`.
 *
 * Nine cases, and the ones that carry the weight are about **who decides** rather than about the vocabulary:
 *
 *   `input.command.vocabulary`  the four families as ranges, and the fact that a family door is a range test
 *                               rather than a table that could disagree with the enum;
 *
 *   `input.command.record`      one command as a value, including the argument rule stated once: an id takes
 *                               the arguments its family takes and refuses the rest, so `MOVE_LEFT` with a text
 *                               argument is a caller's bug rather than a field nothing reads;
 *
 *   `input.command.router`      the chain: innermost first, `RoboNotSupported` continues, a real failure *stops*
 *                               it, and a command nobody claims is `RoboNotFound` **and counted** -- a shortcut
 *                               that silently does nothing being the hardest input bug to see;
 *
 *   `input.command.editor`      the bridge over a real 12.22 editor: typing, deleting, selecting, copying out,
 *                               cutting (which must read *before* it deletes or the text is lost), pasting, the
 *                               two document movements the editor's vocabulary does not name, the line and
 *                               vertical movements **refused by the editor and passed through**, `Undo`/`Redo`
 *                               refused by name, and a focus command declined so a router can keep walking;
 *
 *   `input.command.route_to_editor`  the whole layer end to end: a router with an editor target, a command
 *                               routed, text in the editor, and the target's own counters;
 *
 *   `input.shortcut.vocabulary` the five scopes and the eight directions, positionally with their names;
 *
 *   `input.shortcut.chord`      the normalization that makes two spellings of AltGr **one** chord and keeps the
 *                               locks out of a shortcut's identity;
 *
 *   `input.shortcut.table`      add, the idempotent re-statement, the *conflict* that is refused (one chord
 *                               means one command per scope), the bound, both removals and the enable switch;
 *
 *   `input.shortcut.resolve`    the scope order that makes a focused control win, a disabled row skipped, the
 *                               miss counted, and the event door refusing a release and a repeat so a command
 *                               cannot fire twice;
 *
 *   `input.shortcut.dump`       both dumps: scope order, determinism and `?` for a buffer too small.
 */

#include "RoboInputTestSupport.h"

/* ------------------------------------------------------------------ */
/* Fixtures                                                            */
/* ------------------------------------------------------------------ */

/** An editor over @p text (NUL-terminated), asserting on refusal. */
static RoboTextEditor *EditorWith(const char *text)
{
    RoboTextEditor *editor = NULL;

    if (RoboTextEditorCreate(&editor, NULL) != RoboOk) {
        RoboCheckMsg(0, "the case's editor was refused");
        return NULL;
    }
    if (text && text[0] != '\0') {
        if (RoboTextEditorInsert(editor, text, strlen(text), NULL, NULL) != RoboOk) {
            RoboCheckMsg(0, "the case's seed text was refused");
        }
    }
    return editor;
}

/**
 * The editor's whole text, as a string the caller may print.
 *
 * Read through the editor's **own public doors** -- select everything, then copy the selection out -- rather
 * than by reaching into a buffer, because what the *editor* says its text is, is the thing a command's effect
 * has to be judged by. The selection is left selecting everything, which every case below expects anyway.
 */
static void EditorText(RoboTextEditor *editor, char *out, size_t capacity)
{
    /* The selection is **saved and restored**, so this is a pure read: a helper that left everything selected
     * would make the *next* command act on a selection the case never asked for -- and a delete with everything
     * selected deletes everything. */
    RoboTextSelection saved;
    RoboTextBuffer *buffer = NULL;

    out[0] = '\0';
    if (!editor) {
        return;
    }
    saved = RoboTextEditorSelection(editor);
    if (RoboTextEditorCommand(editor, RoboTextEditCommandSelectAll, NULL, 0u, NULL, NULL, NULL) !=
            RoboOk ||
        RoboTextEditorSelectedText(editor, &buffer, NULL) != RoboOk || !buffer) {
        RoboTextEditorSetSelection(editor, saved);
        strncpy(out, "<unreadable>", capacity - 1u);
        out[capacity - 1u] = '\0';
        return;
    }
    /* An empty buffer's bytes may be **NULL** rather than `""` -- an owned buffer with nothing in it need not
     * carry storage -- so the empty case is the caller's to spell out. */
    {
        const char *bytes = RoboTextBufferBytes(buffer);

        if (bytes && bytes[0] != '\0') {
            strncpy(out, bytes, capacity - 1u);
            out[capacity - 1u] = '\0';
        }
    }
    RoboTextBufferDestroy(buffer);
    RoboTextEditorSetSelection(editor, saved);
}

/** A command target that claims one id and no other, counting what it was asked. */
typedef struct StubTarget {
    InputCommandId claimed;
    uint64_t calls;
    Result failWith;
} StubTarget;

static Result StubExecute(void *userdata, const InputCommand *command, InputDiag *diag)
{
    StubTarget *stub = (StubTarget *)userdata;

    (void)diag;
    stub->calls++;
    if (command->id != stub->claimed) {
        return RoboNotSupported;
    }
    if (stub->failWith != RoboOk) {
        return stub->failWith;
    }
    return RoboOk;
}

static InputCommandTarget StubAsTarget(StubTarget *stub)
{
    InputCommandTarget target;

    InputCommandTargetInit(&target);
    target.userdata = stub;
    target.execute = StubExecute;
    return target;
}

/** A shortcut event: a press at @p physical with @p modifiers. */
static InputEvent ShortcutKey(PhysicalKey physical, InputModifiers modifiers)
{
    InputEvent event = InputKeyEvent(physical, RoboLogicalKeyUnknown, 1, modifiers);

    return event;
}

/* ------------------------------------------------------------------ */
/* The command vocabulary                                              */
/* ------------------------------------------------------------------ */

static void TestCommandVocabulary(void)
{
    RoboCheckEqInt((int)RoboInputCommandNone, 0);
    RoboCheckEqInt((int)RoboInputCommandInsertText, 1);
    RoboCheckEqInt((int)RoboInputCommandTextEditLast, (int)RoboInputCommandRedo);
    RoboCheckEqInt((int)RoboInputCommandFocusNext, 20);
    RoboCheckEqInt((int)RoboInputCommandFocusLast, (int)RoboInputCommandBack);
    RoboCheckEqInt((int)RoboInputCommandScrollLineUp, 26);
    RoboCheckEqInt((int)RoboInputCommandViewLast, (int)RoboInputCommandScrollBy);
    RoboCheckEqInt((int)RoboInputCommandWindowClose, 33);
    RoboCheckEqInt((int)RoboInputCommandWindowLast, (int)RoboInputCommandWindowMenu);
    RoboCheckEqInt((int)RoboInputCommandCount, 40);

    RoboCheckStrEq(InputCommandIdName(RoboInputCommandNone), "none");
    RoboCheckStrEq(InputCommandIdName(RoboInputCommandInsertText), "insert_text");
    RoboCheckStrEq(InputCommandIdName(RoboInputCommandDeleteBackward), "delete_backward");
    RoboCheckStrEq(InputCommandIdName(RoboInputCommandSelectAll), "select_all");
    RoboCheckStrEq(InputCommandIdName(RoboInputCommandCopy), "copy");
    RoboCheckStrEq(InputCommandIdName(RoboInputCommandCut), "cut");
    RoboCheckStrEq(InputCommandIdName(RoboInputCommandPaste), "paste");
    RoboCheckStrEq(InputCommandIdName(RoboInputCommandFocusNext), "focus_next");
    RoboCheckStrEq(InputCommandIdName(RoboInputCommandActivate), "activate");
    RoboCheckStrEq(InputCommandIdName(RoboInputCommandScrollBy), "scroll_by");
    RoboCheckStrEq(InputCommandIdName(RoboInputCommandWindowClose), "window_close");
    RoboCheckStrEq(InputCommandIdName(RoboInputCommandResetInputState), "reset_input_state");
    RoboCheckStrEq(InputCommandIdName((InputCommandId)99), "unknown");

    RoboCheck(!InputCommandIdIsKnown(RoboInputCommandNone));
    RoboCheck(InputCommandIdIsKnown(RoboInputCommandInsertText));
    RoboCheck(InputCommandIdIsKnown(RoboInputCommandQuit));
    RoboCheck(!InputCommandIdIsKnown((InputCommandId)RoboInputCommandCount));

    /* The four families, and the fact that they are **disjoint**: a family door that answered for a neighbour's
     * id would make a router ask the wrong layer. */
    {
        int index;
        int textSeen = 0;
        int focusSeen = 0;
        int viewSeen = 0;
        int windowSeen = 0;

        for (index = 1; index < (int)RoboInputCommandCount; index++) {
            InputCommandId id = (InputCommandId)index;
            int families = InputCommandIdIsTextEdit(id) + InputCommandIdIsFocus(id) +
                           InputCommandIdIsView(id) + InputCommandIdIsWindow(id);

            RoboCheck(families <= 1);
            textSeen += InputCommandIdIsTextEdit(id);
            focusSeen += InputCommandIdIsFocus(id);
            viewSeen += InputCommandIdIsView(id);
            windowSeen += InputCommandIdIsWindow(id);
        }
        RoboCheckEqInt(textSeen, 19);
        RoboCheckEqInt(focusSeen, 6);
        RoboCheckEqInt(viewSeen, 7);
        RoboCheckEqInt(windowSeen, 4);
        /* The tail is deliberately in no family: a system command is nobody's by range. */
        RoboCheck(!InputCommandIdIsTextEdit(RoboInputCommandToggleDebugOverlay));
        RoboCheck(!InputCommandIdIsWindow(RoboInputCommandQuit));
    }

    RoboCheckEqInt((int)RoboInputFocusDirectionNone, 0);
    RoboCheckEqInt((int)RoboInputFocusDirectionNext, 1);
    RoboCheckEqInt((int)RoboInputFocusDirectionCount, 9);
    RoboCheckStrEq(InputFocusDirectionName(RoboInputFocusDirectionNone), "none");
    RoboCheckStrEq(InputFocusDirectionName(RoboInputFocusDirectionNext), "next");
    RoboCheckStrEq(InputFocusDirectionName(RoboInputFocusDirectionPrevious), "previous");
    RoboCheckStrEq(InputFocusDirectionName(RoboInputFocusDirectionDown), "down");
    RoboCheckStrEq(InputFocusDirectionName(RoboInputFocusDirectionLast), "last");
    RoboCheckStrEq(InputFocusDirectionName((InputFocusDirection)99), "unknown");
    RoboCheck(!InputFocusDirectionIsKnown(RoboInputFocusDirectionNone));
    RoboCheck(InputFocusDirectionIsKnown(RoboInputFocusDirectionNext));
    RoboCheck(!InputFocusDirectionIsKnown((InputFocusDirection)RoboInputFocusDirectionCount));
}

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

static void TestCommandRecord(void)
{
    InputCommand command;
    InputCommand other;
    InputDiag diag;
    char line[RoboInputCommandDumpMax];
    size_t length = 0u;

    InputCommandInit(&command);
    RoboCheckEqInt((int)command.id, (int)RoboInputCommandNone);
    RoboCheckStrEq(command.text, "");
    RoboCheckEqInt(command.delta, 0);
    RoboCheck(!InputCommandIsValid(&command));
    RoboCheck(!InputCommandIsValid(NULL));
    RoboCheckStrEq(InputCommandText(NULL), "");

    RoboCheckEqInt(InputCommandSet(&command, RoboInputCommandCopy, &diag), RoboOk);
    RoboCheck(InputCommandIsValid(&command));
    RoboCheckEqInt(command.id, (int)RoboInputCommandCopy);
    /* `NONE` is refused rather than stored: a target must never be asked to execute nothing. */
    RoboCheckEqInt(InputCommandSet(&command, RoboInputCommandNone, &diag), RoboInvalidArgument);
    RoboCheckEqInt((int)command.id, (int)RoboInputCommandNone);
    RoboCheckEqInt(InputCommandSet(&command, (InputCommandId)99, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputCommandSet(NULL, RoboInputCommandCopy, &diag), RoboInvalidArgument);

    /* The argument rule. An insert **must** carry text -- an insert with nothing to insert is a keystroke that
     * produced no text, which 13.11 already refuses to produce -- while a paste of an empty clipboard is a
     * legitimate call the editor answers as a no-op. */
    RoboCheckEqInt(InputCommandSet(&command, RoboInputCommandInsertText, &diag), RoboOk);
    RoboCheck(!InputCommandIsValid(&command));
    RoboCheckEqInt(InputCommandSetText(&command, "hi", &diag), RoboOk);
    RoboCheck(InputCommandIsValid(&command));
    RoboCheckStrEq(command.text, "hi");
    RoboCheckStrEq(InputCommandText(&command), "hi");
    RoboCheckEqInt(InputCommandSet(&command, RoboInputCommandPaste, &diag), RoboOk);
    RoboCheck(InputCommandIsValid(&command));
    RoboCheckEqInt(InputCommandSetText(&command, "pasted", &diag), RoboOk);
    RoboCheckStrEq(command.text, "pasted");

    /* Text on an id that takes none, and a distance on an id that takes none. */
    RoboCheckEqInt(InputCommandSet(&command, RoboInputCommandCopy, &diag), RoboOk);
    RoboCheckEqInt(InputCommandSetText(&command, "x", &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputCommandSetDelta(&command, 4, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputCommandSetDirection(&command, RoboInputFocusDirectionNext, &diag),
                   RoboInvalidArgument);
    RoboCheckEqInt(InputCommandSet(&command, RoboInputCommandScrollBy, &diag), RoboOk);
    RoboCheckEqInt(InputCommandSetDelta(&command, -40, &diag), RoboOk);
    RoboCheckEqInt(command.delta, -40);
    RoboCheck(InputCommandIsValid(&command));
    RoboCheckEqInt(InputCommandSet(&command, RoboInputCommandFocusDirection, &diag), RoboOk);
    /* A directional move with no direction is not a command: nothing would know where to go. */
    RoboCheck(!InputCommandIsValid(&command));
    RoboCheckEqInt(InputCommandSetDirection(&command, RoboInputFocusDirectionDown, &diag), RoboOk);
    RoboCheck(InputCommandIsValid(&command));
    RoboCheckEqInt(command.direction, (int)RoboInputFocusDirectionDown);
    RoboCheckEqInt(InputCommandSetDirection(&command, RoboInputFocusDirectionNone, &diag),
                   RoboInvalidArgument);

    /* Equality and the dump. */
    other = command;
    RoboCheck(InputCommandEqual(&command, &other));
    other.delta = 3;
    RoboCheck(!InputCommandEqual(&command, &other));
    RoboCheck(InputCommandEqual(NULL, NULL));
    RoboCheck(!InputCommandEqual(&command, NULL));

    RoboCheckEqInt(InputCommandSet(&command, RoboInputCommandSelectAll, &diag), RoboOk);
    RoboCheckEqInt(InputCommandWrite(&command, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line,
                   "command=select_all text_edit=1 focus=0 view=0 window=0 text=\"\" delta=0 "
                   "direction=none\n");
    {
        char second[RoboInputCommandDumpMax];
        size_t secondLength = 0u;

        RoboCheckEqInt(InputCommandWrite(&command, second, sizeof(second), &secondLength, &diag), RoboOk);
        RoboCheck(strcmp(line, second) == 0);
    }
    RoboCheckEqInt(InputCommandSet(&command, RoboInputCommandScrollBy, &diag), RoboOk);
    RoboCheckEqInt(InputCommandSetDelta(&command, -3, &diag), RoboOk);
    RoboCheckEqInt(InputCommandWrite(&command, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line,
                   "command=scroll_by text_edit=0 focus=0 view=1 window=0 text=\"\" delta=-3 "
                   "direction=none\n");
    RoboCheckEqInt(InputCommandWrite(NULL, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "command <none>\n");
    {
        char small[8];
        size_t needed = 0u;

        RoboCheckEqInt(InputCommandSet(&command, RoboInputCommandCopy, &diag), RoboOk);
        RoboCheckEqInt(InputCommandWrite(&command, small, sizeof(small), &needed, &diag),
                       RoboNotSupported);
        RoboCheck(needed > sizeof(small));
        RoboCheckStrEq(small, "?");
        RoboCheckEqInt(diag.domain, RoboInputDomainDump);
    }
    RoboCheckEqInt(InputCommandWrite(&command, NULL, sizeof(line), &length, &diag),
                   RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* The router                                                          */
/* ------------------------------------------------------------------ */

static void TestCommandRouter(void)
{
    InputCommandRouter router;
    InputCommand command;
    InputCommandTarget target;
    StubTarget inner;
    StubTarget middle;
    StubTarget outer;
    InputDiag diag;
    int32_t level = -99;
    char line[RoboInputCommandRouterDumpMax];
    size_t length = 0u;

    InputCommandRouterInit(&router);
    InputCommandRouterClear(NULL);
    RoboCheckEqInt((int)InputCommandRouterDepth(&router), 0);
    RoboCheckEqInt((int)InputCommandRouterDepth(NULL), 0);
    RoboCheckEqInt(InputCommandRouterPop(&router, &diag), RoboInvalidArgument);

    {
        InputCommand unknown;

        /* A command nobody claims is `RoboNotFound` **and counted**: a shortcut that silently does nothing is
         * the hardest input bug to see, so the number exists. */
        InputCommandInit(&unknown);
        RoboCheckEqInt(InputCommandSet(&unknown, RoboInputCommandCopy, &diag), RoboOk);
        RoboCheckEqInt(InputCommandRouterRoute(&router, &unknown, &level, &diag), RoboNotFound);
        RoboCheckEqInt(level, -1);
        RoboCheckEqInt((int)router.unclaimedTotal, 1);
    }

    /* An empty chain, a NULL router and an invalid command are all refusals rather than crashes. */
    {
        InputCommand none;

        InputCommandInit(&none);
        RoboCheckEqInt(InputCommandRouterRoute(NULL, &none, NULL, &diag), RoboInvalidArgument);
        RoboCheckEqInt(InputCommandRouterRoute(&router, NULL, NULL, &diag), RoboInvalidArgument);
        RoboCheckEqInt(InputCommandRouterRoute(&router, &none, NULL, &diag), RoboInvalidArgument);
        RoboCheckEqInt(InputCommandRouterPush(&router, NULL, &diag), RoboInvalidArgument);
        InputCommandTargetInit(&target);
        RoboCheckEqInt(InputCommandRouterPush(&router, &target, &diag), RoboInvalidArgument);
        RoboCheckEqInt(InputCommandRouterTargetAt(&router, 0u, &target, &diag), RoboNotFound);
    }

    /* Push outermost first: the window's, the document's, the focused control's. The **last** pushed is asked
     * first, which is what makes "a focused text field wins" a property of the order. */
    inner.claimed = RoboInputCommandCopy;
    inner.calls = 0u;
    inner.failWith = RoboOk;
    middle.claimed = RoboInputCommandCopy;
    middle.calls = 0u;
    middle.failWith = RoboOk;
    outer.claimed = RoboInputCommandCopy;
    outer.calls = 0u;
    outer.failWith = RoboOk;

    target = StubAsTarget(&outer);
    RoboCheckEqInt(InputCommandRouterPush(&router, &target, &diag), RoboOk);
    target = StubAsTarget(&middle);
    RoboCheckEqInt(InputCommandRouterPush(&router, &target, &diag), RoboOk);
    target = StubAsTarget(&inner);
    RoboCheckEqInt(InputCommandRouterPush(&router, &target, &diag), RoboOk);
    RoboCheckEqInt((int)InputCommandRouterDepth(&router), 3);

    /* Level 0 is the **innermost**, because that is the order a command is asked in. */
    RoboCheckEqInt(InputCommandRouterTargetAt(&router, 0u, &target, &diag), RoboOk);
    RoboCheck(target.userdata == (void *)&inner);
    RoboCheckEqInt(InputCommandRouterTargetAt(&router, 2u, &target, &diag), RoboOk);
    RoboCheck(target.userdata == (void *)&outer);

    RoboCheckEqInt(InputCommandSet(&command, RoboInputCommandCopy, &diag), RoboOk);
    RoboCheckEqInt(InputCommandRouterRoute(&router, &command, &level, &diag), RoboOk);
    RoboCheckEqInt(level, 0);
    RoboCheckEqInt((int)inner.calls, 1);
    RoboCheckEqInt((int)middle.calls, 0);
    RoboCheckEqInt((int)outer.calls, 0);
    RoboCheckEqInt((int)router.routedTotal, 1);

    /* Nobody interested: every target is asked and every one declines, which is how a window's target, a
     * document's target and a control's share one chain. */
    inner.claimed = RoboInputCommandQuit;
    middle.claimed = RoboInputCommandQuit;
    outer.claimed = RoboInputCommandQuit;
    RoboCheckEqInt(InputCommandSet(&command, RoboInputCommandSelectAll, &diag), RoboOk);
    RoboCheckEqInt(InputCommandRouterRoute(&router, &command, &level, &diag), RoboNotFound);
    RoboCheckEqInt((int)inner.calls, 2);
    RoboCheckEqInt((int)middle.calls, 1);
    RoboCheckEqInt((int)outer.calls, 1);
    RoboCheckEqInt(level, -1);
    RoboCheckEqInt((int)router.unclaimedTotal, 2);

    /* A target that claims and **fails** stops the walk: handing the same command to a broader target
     * afterwards would undo work the failing one may already have done. */
    inner.claimed = RoboInputCommandSelectAll;
    inner.failWith = RoboError;
    middle.claimed = RoboInputCommandSelectAll;
    middle.calls = 0u;
    outer.calls = 0u;
    RoboCheckEqInt(InputCommandRouterRoute(&router, &command, &level, &diag), RoboError);
    RoboCheckEqInt((int)middle.calls, 0);
    RoboCheckEqInt((int)outer.calls, 0);
    RoboCheckEqInt((int)router.failedTotal, 1);
    inner.failWith = RoboOk;

    /* Pop and clear. */
    RoboCheckEqInt(InputCommandRouterPop(&router, &diag), RoboOk);
    RoboCheckEqInt((int)InputCommandRouterDepth(&router), 2);
    RoboCheckEqInt(InputCommandRouterTargetAt(&router, 0u, &target, &diag), RoboOk);
    RoboCheck(target.userdata == (void *)&middle);
    InputCommandRouterClear(&router);
    RoboCheckEqInt((int)InputCommandRouterDepth(&router), 0);
    /* Clearing keeps the counters: a router is reset, not replaced. */
    RoboCheckEqInt((int)router.routedTotal, 1);

    /* The bound, and the dump. */
    {
        StubTarget filler;
        uint32_t index;

        filler.claimed = RoboInputCommandQuit;
        filler.calls = 0u;
        filler.failWith = RoboOk;
        for (index = 0u; index < RoboInputCommandMaxTargets; index++) {
            target = StubAsTarget(&filler);
            if (InputCommandRouterPush(&router, &target, &diag) != RoboOk) {
                RoboCheckMsg(0, "the chain refused a target before it was full");
                break;
            }
        }
        RoboCheckEqInt((int)InputCommandRouterDepth(&router), (int)RoboInputCommandMaxTargets);
        target = StubAsTarget(&filler);
        RoboCheckEqInt(InputCommandRouterPush(&router, &target, &diag), RoboNotSupported);
        RoboCheckEqInt((int)InputCommandRouterDepth(&router), (int)RoboInputCommandMaxTargets);
    }

    RoboCheckEqInt(InputCommandRouterWrite(&router, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheck(strncmp(line, "router depth=32 routed=1 unclaimed=2 failed=1\n", 45u) == 0);
    RoboCheck(strstr(line, "target 0 ") != NULL);
    RoboCheckEqInt(InputCommandRouterWrite(NULL, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "router <none>\n");
    {
        char small[8];
        size_t needed = 0u;

        RoboCheckEqInt(InputCommandRouterWrite(&router, small, sizeof(small), &needed, &diag),
                       RoboNotSupported);
        RoboCheckStrEq(small, "?");
    }
}

/* ------------------------------------------------------------------ */
/* The editor bridge                                                   */
/* ------------------------------------------------------------------ */

static void TestCommandEditor(void)
{
    RoboTextEditor *editor;
    RoboTextEditResult result;
    RoboTextBuffer *outText = NULL;
    InputCommand command;
    InputDiag diag;
    char text[128];

    /* The mapping table is **total** over the editor's family: every text-editing id answers either an editor
     * command or "expressed by other doors", and no id outside the family answers anything. */
    {
        int index;

        for (index = 1; index < (int)RoboInputCommandCount; index++) {
            InputCommandId id = (InputCommandId)index;
            int isEditor = InputCommandIdIsEditorCommand(id);

            RoboCheckEqInt(isEditor, InputCommandIdIsTextEdit(id));
            if (InputCommandIdIsTextEdit(id)) {
                /* Exactly two ids are executed through the caret doors rather than the command vocabulary. */
                int named = InputCommandToTextEdit(id) != RoboTextEditCommandNone;
                RoboCheck(named || id == RoboInputCommandMoveDocumentStart ||
                          id == RoboInputCommandMoveDocumentEnd);
            } else {
                RoboCheckEqInt((int)InputCommandToTextEdit(id), (int)RoboTextEditCommandNone);
            }
        }
        RoboCheckEqInt((int)InputCommandToTextEdit(RoboInputCommandInsertText),
                       (int)RoboTextEditCommandInsertText);
        RoboCheckEqInt((int)InputCommandToTextEdit(RoboInputCommandSelectAll),
                       (int)RoboTextEditCommandSelectAll);
        RoboCheckEqInt((int)InputCommandToTextEdit(RoboInputCommandMoveDocumentStart),
                       (int)RoboTextEditCommandNone);
    }

    editor = EditorWith("hello");
    RoboCheck(editor != NULL);

    /* Typing: the insert carries the command's text, and it lands at the caret. */
    InputCommandInit(&command);
    RoboCheckEqInt(InputCommandSet(&command, RoboInputCommandInsertText, &diag), RoboOk);
    RoboCheckEqInt(InputCommandSetText(&command, " world", &diag), RoboOk);
    RoboTextEditResultInit(&result);
    RoboCheckEqInt(InputCommandApplyToEditor(&command, editor, NULL, &result, &diag), RoboOk);
    RoboCheck(RoboTextEditResultChanged(&result));
    EditorText(editor, text, sizeof(text));
    RoboCheckStrEq(text, "hello world");
    RoboTextEditResultDestroy(&result);

    /* Backspace twice, and the text says so. */
    RoboCheckEqInt(InputCommandSet(&command, RoboInputCommandDeleteBackward, &diag), RoboOk);
    RoboTextEditResultInit(&result);
    RoboCheckEqInt(InputCommandApplyToEditor(&command, editor, NULL, &result, &diag), RoboOk);
    RoboTextEditResultDestroy(&result);
    EditorText(editor, text, sizeof(text));
    RoboCheckStrEq(text, "hello worl");

    /* Select everything, then a copy that hands the text **out** rather than keeping it: where a copied
     * selection goes is the clipboard's business (13.40/13.41), not a bridge's. */
    RoboCheckEqInt(InputCommandSet(&command, RoboInputCommandSelectAll, &diag), RoboOk);
    RoboTextEditResultInit(&result);
    RoboCheckEqInt(InputCommandApplyToEditor(&command, editor, NULL, &result, &diag), RoboOk);
    RoboTextEditResultDestroy(&result);
    RoboCheckEqInt((int)RoboTextEditorSelectionRange(editor).start, 0);
    RoboCheckEqInt((int)RoboTextEditorSelectionRange(editor).end, 10);

    RoboCheckEqInt(InputCommandSet(&command, RoboInputCommandCopy, &diag), RoboOk);
    RoboTextEditResultInit(&result);
    outText = NULL;
    RoboCheckEqInt(InputCommandApplyToEditor(&command, editor, &outText, &result, &diag), RoboOk);
    RoboCheck(outText != NULL);
    RoboCheckStrEq(RoboTextBufferBytes(outText), "hello worl");
    RoboTextBufferDestroy(outText);
    outText = NULL;
    RoboTextEditResultDestroy(&result);
    /* A copy changes no text. */
    EditorText(editor, text, sizeof(text));
    RoboCheckStrEq(text, "hello worl");

    /* A **cut** must read *before* it deletes, or the text is lost -- which is why the sink is part of the
     * call and not a separate step a caller could forget. */
    RoboCheckEqInt(InputCommandSet(&command, RoboInputCommandCut, &diag), RoboOk);
    RoboTextEditResultInit(&result);
    RoboCheckEqInt(InputCommandApplyToEditor(&command, editor, &outText, &result, &diag), RoboOk);
    RoboCheck(outText != NULL);
    RoboCheckStrEq(RoboTextBufferBytes(outText), "hello worl");
    RoboTextBufferDestroy(outText);
    outText = NULL;
    RoboTextEditResultDestroy(&result);
    EditorText(editor, text, sizeof(text));
    RoboCheckStrEq(text, "");

    /* Paste: the text a caller already holds. */
    RoboCheckEqInt(InputCommandSet(&command, RoboInputCommandPaste, &diag), RoboOk);
    RoboCheckEqInt(InputCommandSetText(&command, "abc", &diag), RoboOk);
    RoboTextEditResultInit(&result);
    RoboCheckEqInt(InputCommandApplyToEditor(&command, editor, NULL, &result, &diag), RoboOk);
    RoboTextEditResultDestroy(&result);
    EditorText(editor, text, sizeof(text));
    RoboCheckStrEq(text, "abc");

    /* The two document movements: a **position**, expressed through the editor's caret door rather than
     * through a vocabulary 12.22 deliberately does not have. They change no text, so the published operation
     * says `changed` 0 rather than an invented edit. */
    RoboCheckEqInt(InputCommandSet(&command, RoboInputCommandMoveDocumentStart, &diag), RoboOk);
    RoboTextEditResultInit(&result);
    RoboCheckEqInt(InputCommandApplyToEditor(&command, editor, NULL, &result, &diag), RoboOk);
    RoboCheck(!RoboTextEditResultChanged(&result));
    RoboTextEditResultDestroy(&result);
    RoboCheckEqInt((int)RoboTextEditorCaret(editor).position.offset, 0);
    RoboCheckEqInt(InputCommandSet(&command, RoboInputCommandMoveDocumentEnd, &diag), RoboOk);
    RoboTextEditResultInit(&result);
    RoboCheckEqInt(InputCommandApplyToEditor(&command, editor, NULL, &result, &diag), RoboOk);
    RoboTextEditResultDestroy(&result);
    RoboCheckEqInt((int)RoboTextEditorCaret(editor).position.offset, 3);

    /* The line and vertical movements are **refused by the editor** and passed through unchanged: they need a
     * layout, and a bridge that approximated them would be a second opinion about editing. */
    {
        static const InputCommandId verticals[] = {
            RoboInputCommandMoveUp, RoboInputCommandMoveDown, RoboInputCommandMoveLineStart,
            RoboInputCommandMoveLineEnd
        };
        size_t index;

        for (index = 0u; index < sizeof(verticals) / sizeof(verticals[0]); index++) {
            RoboTextEditResultInit(&result);
            RoboCheckEqInt(InputCommandSet(&command, verticals[index], &diag), RoboOk);
            RoboCheckEqInt(InputCommandApplyToEditor(&command, editor, NULL, &result, &diag),
                           RoboNotSupported);
            RoboTextEditResultDestroy(&result);
        }
    }

    /* `Undo`/`Redo`: the record exists in every operation, the history does not, and 12.22 refuses them **by
     * name** rather than pretending. */
    RoboCheckEqInt(InputCommandSet(&command, RoboInputCommandUndo, &diag), RoboOk);
    RoboTextEditResultInit(&result);
    RoboCheckEqInt(InputCommandApplyToEditor(&command, editor, NULL, &result, &diag), RoboNotSupported);
    RoboTextEditResultDestroy(&result);

    /* A command from another family is **declined**, not failed: that is how a router keeps walking to the
     * layer that does own it. */
    {
        static const InputCommandId others[] = {
            RoboInputCommandFocusNext, RoboInputCommandActivate, RoboInputCommandScrollLineDown,
            RoboInputCommandWindowClose, RoboInputCommandQuit
        };
        size_t index;

        for (index = 0u; index < sizeof(others) / sizeof(others[0]); index++) {
            RoboTextEditResultInit(&result);
            RoboCheckEqInt(InputCommandSet(&command, others[index], &diag), RoboOk);
            RoboCheckEqInt(InputCommandApplyToEditor(&command, editor, NULL, &result, &diag),
                           RoboNotSupported);
            RoboTextEditResultDestroy(&result);
        }
    }

    /* The arguments, and a NULL editor. */
    RoboCheckEqInt(InputCommandSet(&command, RoboInputCommandCopy, &diag), RoboOk);
    RoboCheckEqInt(InputCommandApplyToEditor(&command, NULL, NULL, NULL, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputCommandApplyToEditor(NULL, editor, NULL, NULL, &diag), RoboInvalidArgument);
    {
        InputCommand none;

        InputCommandInit(&none);
        RoboCheckEqInt(InputCommandApplyToEditor(&none, editor, NULL, NULL, &diag), RoboInvalidArgument);
    }

    RoboTextEditorDestroy(editor);
}

/* ------------------------------------------------------------------ */
/* The command layer, end to end                                       */
/* ------------------------------------------------------------------ */

static void TestCommandRouteToEditor(void)
{
    RoboTextEditor *editor = EditorWith("");
    InputEditorTarget target;
    InputCommandTarget commandTarget;
    InputCommandRouter router;
    InputCommand command;
    InputCommandTarget unclaimed;
    StubTarget outer;
    InputDiag diag;
    int32_t level = -1;
    char line[RoboInputEditorTargetDumpMax];
    size_t length = 0u;
    char text[64];

    RoboCheck(editor != NULL);
    RoboCheckEqInt(InputEditorTargetInit(&target, editor, &diag), RoboOk);
    RoboCheckEqInt(InputEditorTargetInit(NULL, editor, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputEditorTargetInit(&target, NULL, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputEditorTargetAsCommandTarget(&target, &commandTarget, &diag), RoboOk);
    RoboCheck(InputCommandTargetIsValid(&commandTarget));
    RoboCheckEqInt(InputEditorTargetAsCommandTarget(NULL, &commandTarget, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputEditorTargetAsCommandTarget(&target, NULL, &diag), RoboInvalidArgument);

    InputCommandRouterInit(&router);
    /* An outer target that owns a system command, so the walk has somewhere to go past the editor. */
    outer.claimed = RoboInputCommandResetInputState;
    outer.calls = 0u;
    outer.failWith = RoboOk;
    unclaimed = StubAsTarget(&outer);
    RoboCheckEqInt(InputCommandRouterPush(&router, &unclaimed, &diag), RoboOk);
    RoboCheckEqInt(InputCommandRouterPush(&router, &commandTarget, &diag), RoboOk);

    /* A command the editor owns reaches it. */
    InputCommandInit(&command);
    RoboCheckEqInt(InputCommandSet(&command, RoboInputCommandInsertText, &diag), RoboOk);
    RoboCheckEqInt(InputCommandSetText(&command, "typed", &diag), RoboOk);
    RoboCheckEqInt(InputCommandRouterRoute(&router, &command, &level, &diag), RoboOk);
    RoboCheckEqInt(level, 0);
    RoboCheckEqInt((int)target.appliedTotal, 1);
    RoboCheckEqInt((int)target.declinedTotal, 0);
    EditorText(editor, text, sizeof(text));
    RoboCheckStrEq(text, "typed");
    RoboCheck(target.hasLast);
    RoboCheck(RoboTextEditResultChanged(&target.last));

    /* A focus command is declined by the editor and claimed outside it -- the walk working. */
    RoboCheckEqInt(InputCommandSet(&command, RoboInputCommandFocusNext, &diag), RoboOk);
    RoboCheckEqInt(InputCommandRouterRoute(&router, &command, &level, &diag), RoboNotFound);
    RoboCheckEqInt((int)target.declinedTotal, 1);
    /* The outer target was **asked** and declined: a walk that skipped it would be a walk that guessed which
     * targets are interested. */
    RoboCheckEqInt((int)outer.calls, 1);

    RoboCheckEqInt(InputCommandSet(&command, RoboInputCommandResetInputState, &diag), RoboOk);
    RoboCheckEqInt(InputCommandRouterRoute(&router, &command, &level, &diag), RoboOk);
    RoboCheckEqInt(level, 1);
    RoboCheckEqInt((int)outer.calls, 2);
    RoboCheckEqInt((int)target.declinedTotal, 2);

    /* The target's own dump, and its lifetime: the last operation is the *last* one, not a mixture. */
    RoboCheckEqInt(InputEditorTargetWrite(&target, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "editor applied=1 declined=2 has_last=1 last_changed=1 scalars=5\n");
    RoboCheckEqInt(InputEditorTargetWrite(NULL, line, sizeof(line), &length, &diag), RoboOk);
    RoboCheckStrEq(line, "editor <none>\n");
    {
        char small[8];
        size_t needed = 0u;

        RoboCheckEqInt(InputEditorTargetWrite(&target, small, sizeof(small), &needed, &diag),
                       RoboNotSupported);
        RoboCheckStrEq(small, "?");
    }

    InputEditorTargetRelease(&target);
    RoboCheck(!target.hasLast);
    /* A pointer at a new editor drops what the old one left, because the caller says so explicitly. */
    RoboCheckEqInt(InputEditorTargetInit(&target, editor, &diag), RoboOk);
    RoboCheckEqInt((int)target.appliedTotal, 0);
    InputEditorTargetRelease(&target);
    InputEditorTargetRelease(NULL);
    RoboCheckEqInt(InputEditorTargetExecute(NULL, &command, &diag), RoboInvalidArgument);

    RoboTextEditorDestroy(editor);
}

/* ------------------------------------------------------------------ */
/* Shortcuts                                                           */
/* ------------------------------------------------------------------ */

static void TestShortcutVocabulary(void)
{
    RoboCheckEqInt((int)RoboInputShortcutScopeGlobal, 0);
    RoboCheckEqInt((int)RoboInputShortcutScopeDesktop, 1);
    RoboCheckEqInt((int)RoboInputShortcutScopeWindow, 2);
    RoboCheckEqInt((int)RoboInputShortcutScopeDocument, 3);
    RoboCheckEqInt((int)RoboInputShortcutScopeControl, 4);
    RoboCheckEqInt((int)RoboInputShortcutScopeCount, 5);
    RoboCheckStrEq(InputShortcutScopeName(RoboInputShortcutScopeGlobal), "global");
    RoboCheckStrEq(InputShortcutScopeName(RoboInputShortcutScopeDesktop), "desktop");
    RoboCheckStrEq(InputShortcutScopeName(RoboInputShortcutScopeWindow), "window");
    RoboCheckStrEq(InputShortcutScopeName(RoboInputShortcutScopeDocument), "document");
    /* The narrowest is answered **first**, which is why its value is the largest: resolution counts from the
     * inside out. */
    RoboCheckStrEq(InputShortcutScopeName(RoboInputShortcutScopeControl), "control");
    RoboCheckStrEq(InputShortcutScopeName((InputShortcutScope)9), "unknown");
    RoboCheck(InputShortcutScopeIsKnown(RoboInputShortcutScopeGlobal));
    RoboCheck(!InputShortcutScopeIsKnown((InputShortcutScope)RoboInputShortcutScopeCount));
    RoboCheckEqInt((int)RoboInputShortcutMax, 64);
}

static void TestShortcutChord(void)
{
    /* A chord is shift/control/alt/meta and nothing else: a lock does not change what a shortcut means, and a
     * key's *side* is not part of the chord a table stores. */
    RoboCheckEqInt((int)InputModifierChord(RoboInputModifiersNone), 0);
    RoboCheckEqInt((int)InputModifierChord(RoboKeyModCtrl), (int)RoboKeyModCtrl);
    RoboCheckEqInt((int)InputModifierChord(RoboKeyModShift | RoboKeyModCtrl),
                   (int)(RoboKeyModShift | RoboKeyModCtrl));
    RoboCheckEqInt((int)InputModifierChord(RoboKeyModCapsLock), 0);
    RoboCheckEqInt((int)InputModifierChord(RoboKeyModNumLock | RoboKeyModScrollLock), 0);
    RoboCheckEqInt((int)InputModifierChord(RoboKeyModShiftLeft), (int)RoboKeyModShift);
    RoboCheckEqInt((int)InputModifierChord(RoboKeyModCtrlRight), (int)RoboKeyModCtrl);

    /* The one that matters: AltGr's two spellings are **one** chord, so a table written either way is found
     * either way -- and a conflict check that missed it would let two rows claim one chord. */
    RoboCheckEqInt((int)InputModifierChord(RoboKeyModAltRight),
                   (int)(RoboKeyModCtrl | RoboKeyModAlt));
    RoboCheckEqInt((int)InputModifierChord(RoboKeyModCtrl | RoboKeyModAlt),
                   (int)InputModifierChord(RoboKeyModAltRight));
    RoboCheckEqInt((int)InputModifierChord(RoboKeyModAltRight | RoboKeyModShift),
                   (int)(RoboKeyModCtrl | RoboKeyModAlt | RoboKeyModShift));
    /* A bare left Alt is **not** the same chord as AltGr: it is a menu accelerator, and a table that folded
     * the two would make every Alt binding fire on AltGr. */
    RoboCheck(InputModifierChord(RoboKeyModAltLeft) != InputModifierChord(RoboKeyModAltRight));
    RoboCheckEqInt((int)InputModifierChord(RoboKeyModAltLeft), (int)RoboKeyModAlt);
}

static void TestShortcutTable(void)
{
    InputShortcutManager manager;
    InputShortcut shortcut;
    InputShortcut other;
    InputDiag diag;

    InputShortcutManagerInit(&manager);
    RoboCheckEqInt((int)InputShortcutManagerCount(&manager), 0);
    RoboCheckEqInt((int)InputShortcutManagerCount(NULL), 0);
    InputShortcutManagerClear(NULL);

    InputShortcutInit(&shortcut);
    RoboCheckEqInt((int)shortcut.command, (int)RoboInputCommandNone);
    RoboCheck(!InputShortcutIsValid(&shortcut));

    RoboCheckEqInt(InputShortcutSet(&shortcut, RoboInputCommandCopy, RoboPhysicalKeyC, RoboKeyModCtrl,
                                    RoboInputShortcutScopeControl, &diag),
                   RoboOk);
    RoboCheck(InputShortcutIsValid(&shortcut));
    RoboCheckEqInt((int)shortcut.modifiers, (int)RoboKeyModCtrl);
    RoboCheck(shortcut.enabled);

    /* The refusals: each is a different rule, and each leaves an empty record. */
    {
        InputShortcut refused;

        InputShortcutInit(&refused);
        RoboCheckEqInt(InputShortcutSet(&refused, RoboInputCommandNone, RoboPhysicalKeyC, RoboKeyModCtrl,
                                        RoboInputShortcutScopeGlobal, &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt((int)refused.command, (int)RoboInputCommandNone);
        RoboCheckEqInt(InputShortcutSet(&refused, RoboInputCommandCopy, RoboPhysicalKeyUnknown,
                                        RoboKeyModCtrl, RoboInputShortcutScopeGlobal, &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt(InputShortcutSet(&refused, RoboInputCommandCopy, RoboPhysicalKeyC,
                                        (InputModifiers)(1u << 30), RoboInputShortcutScopeGlobal, &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt(InputShortcutSet(&refused, RoboInputCommandCopy, RoboPhysicalKeyC, RoboKeyModCtrl,
                                        (InputShortcutScope)9, &diag),
                       RoboInvalidArgument);
        RoboCheckEqInt(InputShortcutSet(NULL, RoboInputCommandCopy, RoboPhysicalKeyC, RoboKeyModCtrl,
                                        RoboInputShortcutScopeGlobal, &diag),
                       RoboInvalidArgument);
        RoboCheck(!InputShortcutIsValid(&refused));
        RoboCheck(!InputShortcutIsValid(NULL));
    }

    RoboCheckEqInt(InputShortcutManagerAdd(&manager, &shortcut, &diag), RoboOk);
    RoboCheckEqInt((int)InputShortcutManagerCount(&manager), 1);

    /* Re-stating the same command on the same chord is **idempotent**: a caller that re-registers its table
     * does not have to know what it did last. */
    RoboCheckEqInt(InputShortcutManagerAdd(&manager, &shortcut, &diag), RoboOk);
    RoboCheckEqInt((int)InputShortcutManagerCount(&manager), 1);

    /* A different command on the same chord in the same scope is a **conflict** rather than an override,
     * because an override would make the answer depend on registration order. */
    InputShortcutInit(&other);
    RoboCheckEqInt(InputShortcutSet(&other, RoboInputCommandCut, RoboPhysicalKeyC, RoboKeyModCtrl,
                                    RoboInputShortcutScopeControl, &diag),
                   RoboOk);
    RoboCheckEqInt(InputShortcutManagerAdd(&manager, &other, &diag), RoboAlreadyExists);
    RoboCheckEqInt((int)InputShortcutManagerCount(&manager), 1);
    /* ... while the *same* chord in another scope is a different binding entirely, which is what scopes are. */
    RoboCheckEqInt(InputShortcutSet(&other, RoboInputCommandCut, RoboPhysicalKeyC, RoboKeyModCtrl,
                                    RoboInputShortcutScopeDocument, &diag),
                   RoboOk);
    RoboCheckEqInt(InputShortcutManagerAdd(&manager, &other, &diag), RoboOk);
    RoboCheckEqInt((int)InputShortcutManagerCount(&manager), 2);

    RoboCheckEqInt(InputShortcutManagerAdd(NULL, &shortcut, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputShortcutManagerAdd(&manager, NULL, &diag), RoboInvalidArgument);
    InputShortcutInit(&other);
    RoboCheckEqInt(InputShortcutManagerAdd(&manager, &other, &diag), RoboInvalidArgument);

    /* At and the two removals. */
    RoboCheckEqInt(InputShortcutManagerAt(&manager, 0u, &other, &diag), RoboOk);
    RoboCheckEqInt((int)other.command, (int)RoboInputCommandCopy);
    RoboCheckEqInt(InputShortcutManagerAt(&manager, 2u, &other, &diag), RoboNotFound);
    RoboCheckEqInt(InputShortcutManagerAt(NULL, 0u, &other, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputShortcutManagerRemoveChord(&manager, RoboPhysicalKeyC, RoboKeyModCtrl,
                                                   RoboInputShortcutScopeDocument, &diag),
                   RoboOk);
    RoboCheckEqInt((int)InputShortcutManagerCount(&manager), 1);
    RoboCheckEqInt(InputShortcutManagerRemoveChord(&manager, RoboPhysicalKeyC, RoboKeyModCtrl,
                                                   RoboInputShortcutScopeDocument, &diag),
                   RoboNotFound);
    /* A chord that is *stored* as `Ctrl` is removed by the `AltRight` spelling too, because they are one
     * chord. */
    InputShortcutInit(&other);
    RoboCheckEqInt(InputShortcutSet(&other, RoboInputCommandPaste, RoboPhysicalKeyV, RoboKeyModAltRight,
                                    RoboInputShortcutScopeControl, &diag),
                   RoboOk);
    RoboCheckEqInt(InputShortcutManagerAdd(&manager, &other, &diag), RoboOk);
    RoboCheckEqInt(InputShortcutManagerRemoveChord(&manager, RoboPhysicalKeyV,
                                                   RoboKeyModCtrl | RoboKeyModAlt,
                                                   RoboInputShortcutScopeControl, &diag),
                   RoboOk);
    RoboCheckEqInt((int)InputShortcutManagerCount(&manager), 1);

    RoboCheckEqInt(InputShortcutManagerRemoveCommand(&manager, RoboInputCommandCopy,
                                                     RoboInputShortcutScopeControl, &diag),
                   RoboOk);
    RoboCheckEqInt((int)InputShortcutManagerCount(&manager), 0);
    RoboCheckEqInt(InputShortcutManagerRemoveCommand(&manager, RoboInputCommandCopy,
                                                     RoboInputShortcutScopeControl, &diag),
                   RoboNotFound);
    RoboCheckEqInt(InputShortcutManagerRemoveCommand(NULL, RoboInputCommandCopy,
                                                     RoboInputShortcutScopeControl, &diag),
                   RoboInvalidArgument);

    /* Find-in-scope-order: with the same command bound twice, the answer is the binding that would fire. */
    {
        InputShortcut global;
        InputShortcut control;

        InputShortcutInit(&global);
        RoboCheckEqInt(InputShortcutSet(&global, RoboInputCommandSelectAll, RoboPhysicalKeyA,
                                        RoboKeyModCtrl, RoboInputShortcutScopeGlobal, &diag),
                       RoboOk);
        InputShortcutInit(&control);
        RoboCheckEqInt(InputShortcutSet(&control, RoboInputCommandSelectAll, RoboPhysicalKeyA,
                                        RoboKeyModCtrl, RoboInputShortcutScopeControl, &diag),
                       RoboOk);
        RoboCheckEqInt(InputShortcutManagerAdd(&manager, &global, &diag), RoboOk);
        RoboCheckEqInt(InputShortcutManagerAdd(&manager, &control, &diag), RoboOk);
        RoboCheckEqInt(InputShortcutManagerFindCommand(&manager, RoboInputCommandSelectAll, &other, &diag),
                       RoboOk);
        RoboCheckEqInt((int)other.scope, (int)RoboInputShortcutScopeControl);
        RoboCheckEqInt(InputShortcutManagerFindCommand(&manager, RoboInputCommandWindowClose, &other,
                                                       &diag),
                       RoboNotFound);
    }

    /* The enable switch: a disabled row never resolves and is still findable, which is what makes a mode
     * switchable rather than a table to rebuild. */
    RoboCheckEqInt(InputShortcutManagerSetEnabled(&manager, RoboInputCommandSelectAll,
                                                  RoboInputShortcutScopeControl, 0, &diag),
                   RoboOk);
    RoboCheckEqInt(InputShortcutManagerResolve(&manager, RoboPhysicalKeyA, RoboKeyModCtrl, &other, &diag),
                   RoboOk);
    RoboCheckEqInt((int)other.scope, (int)RoboInputShortcutScopeGlobal);
    RoboCheckEqInt(InputShortcutManagerSetEnabled(&manager, RoboInputCommandSelectAll,
                                                  RoboInputShortcutScopeControl, 1, &diag),
                   RoboOk);
    RoboCheckEqInt(InputShortcutManagerSetEnabled(&manager, RoboInputCommandWindowClose,
                                                  RoboInputShortcutScopeControl, 1, &diag),
                   RoboNotFound);
    RoboCheckEqInt(InputShortcutManagerSetEnabled(NULL, RoboInputCommandSelectAll,
                                                  RoboInputShortcutScopeControl, 1, &diag),
                   RoboInvalidArgument);

    /* The bound. */
    {
        InputShortcut filler;
        uint32_t index;

        InputShortcutManagerInit(&manager);
        InputShortcutInit(&filler);
        for (index = 0u; index < RoboInputShortcutMax; index++) {
            /* Distinct keys and levels, so every row is a different chord. */
            PhysicalKey key = (PhysicalKey)(1u + index);

            if (InputShortcutSet(&filler, RoboInputCommandCopy, key, RoboInputModifiersNone,
                                 RoboInputShortcutScopeGlobal, &diag) != RoboOk) {
                RoboCheckMsg(0, "the case's own shortcut was refused");
                break;
            }
            if (InputShortcutManagerAdd(&manager, &filler, &diag) != RoboOk) {
                RoboCheckMsg(0, "the table refused a row before it was full");
                break;
            }
        }
        RoboCheckEqInt((int)InputShortcutManagerCount(&manager), (int)RoboInputShortcutMax);
        RoboCheckEqInt(InputShortcutSet(&filler, RoboInputCommandCopy, RoboPhysicalKeySlash,
                                        RoboKeyModMeta, RoboInputShortcutScopeGlobal, &diag),
                       RoboOk);
        RoboCheckEqInt(InputShortcutManagerAdd(&manager, &filler, &diag), RoboNotSupported);
    }

    /* Equality, as chords. */
    {
        InputShortcut left;
        InputShortcut right;

        InputShortcutInit(&left);
        InputShortcutInit(&right);
        RoboCheckEqInt(InputShortcutSet(&left, RoboInputCommandPaste, RoboPhysicalKeyV, RoboKeyModAltRight,
                                        RoboInputShortcutScopeControl, &diag),
                       RoboOk);
        RoboCheckEqInt(InputShortcutSet(&right, RoboInputCommandPaste, RoboPhysicalKeyV,
                                        RoboKeyModCtrl | RoboKeyModAlt, RoboInputShortcutScopeControl,
                                        &diag),
                       RoboOk);
        /* Two spellings of one fact, and a conflict check that missed it would let two rows claim a chord. */
        RoboCheck(InputShortcutEqual(&left, &right));
        RoboCheck(InputShortcutEqual(NULL, NULL));
        RoboCheck(!InputShortcutEqual(&left, NULL));
    }
}

static void TestShortcutResolve(void)
{
    InputShortcutManager manager;
    InputShortcut shortcut;
    InputShortcut resolved;
    InputDiag diag;
    InputEvent event;

    InputShortcutManagerInit(&manager);

    /* A global "select all" and a control-scope `Ctrl+C`: the control one wins, which is the whole of the
     * brief's "a focused text field should be able to consume a shortcut before a global action where
     * appropriate" -- with no text field named anywhere. */
    RoboCheckEqInt(InputShortcutSet(&shortcut, RoboInputCommandQuit, RoboPhysicalKeyQ, RoboKeyModCtrl,
                                    RoboInputShortcutScopeGlobal, &diag),
                   RoboOk);
    RoboCheckEqInt(InputShortcutManagerAdd(&manager, &shortcut, &diag), RoboOk);
    RoboCheckEqInt(InputShortcutSet(&shortcut, RoboInputCommandSelectAll, RoboPhysicalKeyA, RoboKeyModCtrl,
                                    RoboInputShortcutScopeGlobal, &diag),
                   RoboOk);
    RoboCheckEqInt(InputShortcutManagerAdd(&manager, &shortcut, &diag), RoboOk);
    RoboCheckEqInt(InputShortcutSet(&shortcut, RoboInputCommandCopy, RoboPhysicalKeyC, RoboKeyModCtrl,
                                    RoboInputShortcutScopeControl, &diag),
                   RoboOk);
    RoboCheckEqInt(InputShortcutManagerAdd(&manager, &shortcut, &diag), RoboOk);

    /* One chord, one command per scope: a different command on the same chord in the same scope is a conflict
     * even here, in the table the resolution cases use. */
    {
        InputShortcut conflict;

        InputShortcutInit(&conflict);
        RoboCheckEqInt(InputShortcutSet(&conflict, RoboInputCommandCut, RoboPhysicalKeyC, RoboKeyModCtrl,
                                        RoboInputShortcutScopeControl, &diag),
                       RoboOk);
        RoboCheckEqInt(InputShortcutManagerAdd(&manager, &conflict, &diag), RoboAlreadyExists);
    }

    RoboCheckEqInt(InputShortcutManagerResolve(&manager, RoboPhysicalKeyC, RoboKeyModCtrl, &resolved, &diag),
                   RoboOk);
    RoboCheckEqInt((int)resolved.command, (int)RoboInputCommandCopy);
    RoboCheckEqInt((int)resolved.scope, (int)RoboInputShortcutScopeControl);
    RoboCheckEqInt((int)manager.resolvedTotal, 1);

    /* The modifer state arrives as reported, locks and sides included: resolution normalizes, so no caller has
     * to. */
    RoboCheckEqInt(InputShortcutManagerResolve(&manager, RoboPhysicalKeyC,
                                               RoboKeyModCtrl | RoboKeyModCtrlLeft | RoboKeyModCapsLock,
                                               &resolved, &diag),
                   RoboOk);
    RoboCheckEqInt((int)resolved.command, (int)RoboInputCommandCopy);

    /* A miss is counted rather than silent. */
    RoboCheckEqInt(InputShortcutManagerResolve(&manager, RoboPhysicalKeyZ, RoboInputModifiersNone, &resolved,
                                               &diag),
                   RoboNotFound);
    RoboCheckEqInt((int)manager.missesTotal, 1);
    /* `Ctrl+Q` is the runtime's, and it is not the same chord as plain `Q`. */
    RoboCheckEqInt(InputShortcutManagerResolve(&manager, RoboPhysicalKeyQ, RoboKeyModCtrl, &resolved, &diag),
                   RoboOk);
    RoboCheckEqInt((int)resolved.command, (int)RoboInputCommandQuit);
    RoboCheckEqInt(InputShortcutManagerResolve(&manager, RoboPhysicalKeyQ, RoboInputModifiersNone, &resolved,
                                               &diag),
                   RoboNotFound);
    RoboCheckEqInt(InputShortcutManagerResolve(NULL, RoboPhysicalKeyQ, RoboInputModifiersNone, &resolved,
                                               &diag),
                   RoboInvalidArgument);
    /* A NULL out is allowed: the caller may want only the answer. */
    RoboCheckEqInt(InputShortcutManagerResolve(&manager, RoboPhysicalKeyQ, RoboKeyModCtrl, NULL, &diag),
                   RoboOk);

    /* The event door: a press resolves, a **release** and a **repeat** do not -- a table that answered either
     * would fire every command twice -- and a pointer event has no chord at all. */
    event = ShortcutKey(RoboPhysicalKeyC, RoboKeyModCtrl);
    RoboCheckEqInt(InputShortcutManagerResolveEvent(&manager, &event, &resolved, &diag), RoboOk);
    RoboCheckEqInt((int)resolved.command, (int)RoboInputCommandCopy);
    event = InputKeyEvent(RoboPhysicalKeyC, RoboLogicalKeyUnknown, 0, RoboKeyModCtrl);
    RoboCheckEqInt(InputShortcutManagerResolveEvent(&manager, &event, &resolved, &diag), RoboNotSupported);
    {
        InputEvent repeat = event;

        repeat.type = RoboInputEventKeyRepeat;
        repeat.payload.keyboard.action = RoboKeyActionDown;
        repeat.payload.keyboard.repeat = 1;
        RoboCheckEqInt(InputShortcutManagerResolveEvent(&manager, &repeat, &resolved, &diag),
                       RoboNotSupported);
    }
    event = InputPointerMove(3, 3, 0u, RoboInputSpaceScreen, 3u);
    RoboCheckEqInt(InputShortcutManagerResolveEvent(&manager, &event, &resolved, &diag), RoboNotSupported);
    RoboCheckEqInt(InputShortcutManagerResolveEvent(NULL, &event, &resolved, &diag), RoboInvalidArgument);
    RoboCheckEqInt(InputShortcutManagerResolveEvent(&manager, NULL, &resolved, &diag), RoboInvalidArgument);
}

static void TestShortcutDump(void)
{
    InputShortcutManager manager;
    InputShortcut shortcut;
    InputDiag diag;
    static char first[RoboInputShortcutManagerDumpMax];
    static char second[RoboInputShortcutManagerDumpMax];
    size_t firstLength = 0u;
    size_t secondLength = 0u;

    InputShortcutManagerInit(&manager);
    /* Registered outermost-first, dumped innermost-first: the text reads in the order resolution uses. */
    RoboCheckEqInt(InputShortcutSet(&shortcut, RoboInputCommandQuit, RoboPhysicalKeyQ, RoboKeyModCtrl,
                                    RoboInputShortcutScopeGlobal, &diag),
                   RoboOk);
    RoboCheckEqInt(InputShortcutManagerAdd(&manager, &shortcut, &diag), RoboOk);
    RoboCheckEqInt(InputShortcutSet(&shortcut, RoboInputCommandCopy, RoboPhysicalKeyC, RoboKeyModCtrl,
                                    RoboInputShortcutScopeControl, &diag),
                   RoboOk);
    RoboCheckEqInt(InputShortcutManagerAdd(&manager, &shortcut, &diag), RoboOk);

    RoboCheckEqInt(InputShortcutManagerWrite(&manager, first, sizeof(first), &firstLength, &diag), RoboOk);
    RoboCheckStrEq(first,
                   "shortcuts count=2 resolved=0 misses=0\n"
                   "shortcut scope=control physical=C modifiers=ctrl command=copy enabled=1\n"
                   "shortcut scope=global physical=Q modifiers=ctrl command=quit enabled=1\n");

    RoboCheckEqInt(InputShortcutManagerWrite(&manager, second, sizeof(second), &secondLength, &diag),
                   RoboOk);
    RoboCheckEqInt((int)secondLength, (int)firstLength);
    RoboCheck(strcmp(first, second) == 0);

    /* A chord with no modifier prints `none`, so a reader can tell a bare key from a modified one. */
    RoboCheckEqInt(InputShortcutSet(&shortcut, RoboInputCommandActivate, RoboPhysicalKeyEnter,
                                    RoboInputModifiersNone, RoboInputShortcutScopeControl, &diag),
                   RoboOk);
    RoboCheckEqInt(InputShortcutManagerAdd(&manager, &shortcut, &diag), RoboOk);
    RoboCheckEqInt(InputShortcutManagerWrite(&manager, first, sizeof(first), &firstLength, &diag), RoboOk);
    RoboCheck(strstr(first, "physical=Enter modifiers=none command=activate") != NULL);

    RoboCheckEqInt(InputShortcutManagerWrite(NULL, first, sizeof(first), &firstLength, &diag), RoboOk);
    RoboCheckStrEq(first, "shortcuts <none>\n");
    {
        char small[8];
        size_t needed = 0u;

        RoboCheckEqInt(InputShortcutManagerWrite(&manager, small, sizeof(small), &needed, &diag),
                       RoboNotSupported);
        RoboCheckStrEq(small, "?");
        RoboCheck(needed > sizeof(small));
        RoboCheckEqInt(diag.domain, RoboInputDomainDump);
    }
    RoboCheckEqInt(InputShortcutManagerWrite(&manager, NULL, sizeof(first), &firstLength, &diag),
                   RoboInvalidArgument);
}

void TestRegisterInputCommand(void)
{
    TestRegister("input.command.vocabulary", TestCommandVocabulary);
    TestRegister("input.command.record", TestCommandRecord);
    TestRegister("input.command.router", TestCommandRouter);
    TestRegister("input.command.editor", TestCommandEditor);
    TestRegister("input.command.route_to_editor", TestCommandRouteToEditor);
    TestRegister("input.shortcut.vocabulary", TestShortcutVocabulary);
    TestRegister("input.shortcut.chord", TestShortcutChord);
    TestRegister("input.shortcut.table", TestShortcutTable);
    TestRegister("input.shortcut.resolve", TestShortcutResolve);
    TestRegister("input.shortcut.dump", TestShortcutDump);
}
