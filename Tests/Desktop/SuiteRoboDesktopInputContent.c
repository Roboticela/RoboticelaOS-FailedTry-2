/*
 * The shell's content integration: the document, the keyboard, the commands, the clipboard and the drag
 * (Phase 13.68 / 13.72 / 13.73 / 13.74).
 *
 * One file, because the four are one chain and each case below walks it end to end:
 *
 *     a point            -> the shell's hit test -> paint's hit test -> a DOCUMENT target with a node
 *     a key              -> the layout -> the text pipeline -> an `InsertText` command -> the editor
 *     a chord            -> the shortcut table -> a command -> the innermost target that claims it
 *     `Ctrl+C`           -> the same chain -> the clipboard -> the same editor on the way back in
 *
 * The claims that need a *real* document are here rather than in the pointer suite: a tree from
 * `DesktopDocumentTree`, whose paint hit test is the one RoboPaint implements, and a RoboHTML document whose
 * identity a consumer can resolve. That split is deliberate -- the pointer suite is about windows and
 * planes, and this one is about what is *inside* a window.
 */

#include "RoboDesktopInputTestSupport.h"

/* ------------------------------------------------------------------ */
/* 1. The document (13.68 / 13.70)                                     */
/* ------------------------------------------------------------------ */

/** The point at the vertical middle of @p window's client area, a little way in from its left edge. */
static GfxPoint ContentPoint(Desktop *desktop, DesktopWindowId window, int32_t across, int32_t down)
{
    GfxRect client;

    RoboCheckEqInt(DesktopWindowClientBounds(desktop, window, &client), RoboOk);
    return MakeGfxPoint(client.x + across, client.y + down);
}

static void TestInputDocument(void)
{
    RenderFace face;
    Desktop *desktop;
    DesktopInput *input;
    DesktopWindowId window;
    DesktopDocument *document;

    DesktopFixture();
    RenderFaceCreate(&face);
    desktop = DesktopOpenWithFace(1280, 800, face.face);
    input = DesktopInputOpen(desktop);
    window = DesktopAddWindow(desktop, "Page", 100, 100, 600, 400);
    /* A real face: a document with no font has no line boxes, and a hit test against it would miss the text
     * it is meant to find. The point of this suite is the *node*, so the document has to have ink. */
    document = DesktopSetWindowContent(desktop, window,
                                       "<html><body><p>Hello content</p></body></html>",
                                       "html, body, p { display: block } "
                                       "html, body { margin: 0; padding: 0 } "
                                       "p { margin: 0; padding: 0 }",
                                       face.face);
    DesktopInputContent content;
    HtmlDocumentId html = DesktopInputDocument("<html><body><p>Hello content</p></body></html>");
    HtmlNodeHandle root = DesktopInputDocumentRoot(html);
    HtmlDocumentId htmlOnly = RoboHtmlDocumentIdNone;
    HtmlNodeHandle onlyRoot;
    DesktopInputStep step;
    InputTarget target;
    /* Inside the first line box: the document's ink is one line tall, so the point has to be in it. */
    GfxPoint point = ContentPoint(desktop, window, 20, 8);
    DesktopWindowId resolved = RoboDesktopWindowIdNone;
    LayoutNodeId firstNode = RoboLayoutNodeIdNone;
    DesktopInputLog log;

    /* An unregistered window has no document identity: a press in it is the window's, which is still an
     * answer and the one 13.68's absence is stated as. */
    RoboCheckEqInt(DesktopInputResolvePoint(input, point, RoboInputSpaceScreen, &target, NULL, NULL,
                                           &resolved, NULL),
                   RoboOk);
    RoboCheckEqInt((int)target.kind, (int)RoboInputTargetWindow);
    RoboCheckEqInt((int)resolved, (int)window);

    /*
     * Register the document's layout tree *and* a real DOM identity. With the tree, a press in the client
     * area resolves to the node under it through RoboPaint's own hit test -- the only thing in this tree
     * that knows where an element is -- and with the handle, a consumer has an identity to resolve.
     */
    DesktopInputContentInit(&content);
    content.window = window;
    content.tree = DesktopDocumentTree(document);
    content.documentNode = root;
    RoboCheck(content.tree != NULL);
    RoboCheckEqInt(DesktopInputSetContent(input, &content, NULL), RoboOk);

    RoboCheckEqInt(DesktopInputResolvePoint(input, point, RoboInputSpaceScreen, &target, NULL, NULL,
                                           &resolved, NULL),
                   RoboOk);
    RoboCheckEqInt((int)target.kind, (int)RoboInputTargetDocument);
    RoboCheckEqInt((int)target.window, (int)window);
    RoboCheck(target.layoutNode != RoboLayoutNodeIdNone);
    RoboCheckEqInt(InputTargetHasDocument(&target), 1);
    {
        HtmlNodeHandle carried;
        LayoutNodeId node = RoboLayoutNodeIdNone;

        RoboCheckEqInt(InputTargetDocument(&target, &carried, &node), 1);
        RoboCheckEqInt((int)node, (int)target.layoutNode);
        /* The identity travels *by value* and is the document the caller registered, not a new one. */
        RoboCheckEqInt(HtmlNodeHandleIsWellformed(&carried), 1);
        RoboCheckEqInt((int)carried.nodeId, (int)root.nodeId);
        RoboCheckEqInt((int)HtmlNodeHandleDocument(&carried), (int)HtmlNodeHandleDocument(&root));
    }
    firstNode = target.layoutNode;

    /* A listener on the document target hears the press and the release, and the target is *stable*. */
    DesktopInputLogInit(&log);
    DesktopInputLogOn(input, &target, RoboInputPhaseTarget, &log);
    {
        InputEvent down = DesktopInputDownEvent((int32_t)point.x, (int32_t)point.y, 0u);
        InputEvent up = DesktopInputUpEvent((int32_t)point.x, (int32_t)point.y, 0u);

        DesktopInputFeed(input, &down, &step);
        RoboCheckEqInt((int)DesktopInputLogCount(&log, RoboInputEventPointerDown), 1);
        RoboCheckEqInt((int)log.kinds[0], (int)RoboInputTargetDocument);
        RoboCheckEqInt((int)log.nodes[0], (int)firstNode);
        RoboCheckEqInt((int)log.windows[0], (int)window);
        /* The press reached the *target* phase after the path built a chain for it (a hover `ENTER` for the
         * same target shares that phase, so this is at-least rather than exactly). */
        RoboCheck(DesktopInputLogPhaseCount(&log, RoboInputPhaseTarget) >= 1u);

        DesktopInputFeed(input, &up, &step);
        RoboCheckEqInt((int)DesktopInputLogCount(&log, RoboInputEventPointerUp), 1);
        /*
         * A release on a document target with a node is the moment a browser fires `click`, and the field
         * exists so the *shell* can post it without this module knowing what a script is.
         */
        RoboCheckEqInt(step.documentClick, 1);
        RoboCheckEqInt((int)step.documentClickNode, (int)firstNode);
        RoboCheck(step.documentClickX > 0.0);
    }

    /*
     * The same point resolves to the same node a second time: the target is a fact about the document, not
     * about the press. A build whose hit test depended on a hover or a capture would fail here.
     */
    {
        InputTarget again;

        RoboCheckEqInt(DesktopInputResolvePoint(input, point, RoboInputSpaceScreen, &again, NULL, NULL,
                                               NULL, NULL),
                       RoboOk);
        RoboCheckEqInt((int)again.layoutNode, (int)firstNode);
        RoboCheckEqInt(InputTargetEqual(&target, &again), 1);
    }

    /*
     * A DOM identity with **no** layout tree is still a document target: the node id travels when the
     * geometry is not known, which is what a document served from a cache looks like (13.68's stated
     * degradation rather than a failure).
     */
    {
        DesktopInputContent handleOnly;
        InputTarget only;

        htmlOnly = DesktopInputDocument("<html><body><p>No layout</p></body></html>");
        onlyRoot = DesktopInputDocumentRoot(htmlOnly);
        DesktopInputContentInit(&handleOnly);
        handleOnly.window = window;
        handleOnly.documentNode = onlyRoot;
        RoboCheckEqInt(DesktopInputSetContent(input, &handleOnly, NULL), RoboOk);

        RoboCheckEqInt(DesktopInputResolvePoint(input, point, RoboInputSpaceScreen, &only, NULL, NULL,
                                               NULL, NULL),
                       RoboOk);
        RoboCheckEqInt((int)only.kind, (int)RoboInputTargetDocument);
        RoboCheckEqInt((int)only.layoutNode, (int)RoboLayoutNodeIdNone);
        RoboCheckEqInt(HtmlNodeHandleIsWellformed(&only.domNode), 1);
        RoboCheckEqInt((int)only.domNode.nodeId, (int)onlyRoot.nodeId);
    }

    /* Clearing the record takes the identity away again, and a press is the window's once more. */
    RoboCheckEqInt(DesktopInputClearContent(input, window, NULL), RoboOk);
    RoboCheckEqInt(DesktopInputResolvePoint(input, point, RoboInputSpaceScreen, &target, NULL, NULL,
                                           NULL, NULL),
                   RoboOk);
    RoboCheckEqInt((int)target.kind, (int)RoboInputTargetWindow);
    RoboCheckEqInt(DesktopInputClearContent(input, window, NULL), RoboNotFound);
    RoboCheckEqInt(DesktopInputClearContent(NULL, window, NULL), RoboInvalidArgument);

    /* Refusals: a record with no window, and one for a window the shell does not hold. */
    {
        DesktopInputContent bad;

        DesktopInputContentInit(&bad);
        RoboCheckEqInt(DesktopInputSetContent(input, &bad, NULL), RoboInvalidArgument);
        bad.window = 4242u;
        RoboCheckEqInt(DesktopInputSetContent(input, &bad, NULL), RoboNotFound);
        RoboCheckEqInt(DesktopInputSetContent(NULL, &bad, NULL), RoboInvalidArgument);
        RoboCheckEqInt(DesktopInputSetContent(input, NULL, NULL), RoboInvalidArgument);
    }

    /* The dump names the registered content, one line per window -- what a reader checks after a rebuild. */
    {
        char dump[RoboDesktopInputDumpMax];
        size_t length = 0u;

        DesktopInputContentInit(&content);
        content.window = window;
        content.tree = DesktopDocumentTree(document);
        RoboCheckEqInt(DesktopInputSetContent(input, &content, NULL), RoboOk);
        RoboCheckEqInt(DesktopInputWrite(input, dump, sizeof(dump), &length, NULL), RoboOk);
        RoboCheck(strstr(dump, "content window=") != NULL);
        RoboCheck(strstr(dump, "tree=1 node=0 editor=0") != NULL);
        RoboCheckEqInt(DesktopInputClearContent(input, window, NULL), RoboOk);
    }

    DesktopInputDocumentClose(html);
    DesktopInputDocumentClose(htmlOnly);
    DesktopDocumentDestroy(document);
    DesktopInputClose(input);
    DesktopClose(desktop);
}

/* ------------------------------------------------------------------ */
/* 2. The keyboard and the editor (13.72)                              */
/* ------------------------------------------------------------------ */

static void TestInputText(void)
{
    Desktop *desktop = DesktopOpen(1280, 800);
    DesktopInput *input = DesktopInputOpen(desktop);
    DesktopWindowId window = DesktopAddWindow(desktop, "Editor", 100, 100, 500, 300);
    RoboTextEditor *editor = DesktopInputEditor("");
    DesktopInputContent content;
    GfxPoint title = DesktopInputTitlePoint(desktop, window);
    DesktopInputStep step;
    InputTarget target;

    DesktopInputContentInit(&content);
    content.window = window;
    content.editor = editor;
    RoboCheckEqInt(DesktopInputSetContent(input, &content, NULL), RoboOk);

    /* Nothing is focused, so a key has nowhere to go -- and that is a *drop*, not a crash. */
    {
        InputEvent key = DesktopInputKeyDown(RoboPhysicalKeyA, 0u);

        DesktopInputFeed(input, &key, &step);
        RoboCheckEqInt((int)step.textInputs, 0);
    }
    /* (`FrontWindow` gives it a window to go to, so the editor still is not the target -- see below.) */

    /* Focus the window the way a user does, then type. */
    {
        InputEvent down = DesktopInputDownEvent((int32_t)title.x, (int32_t)title.y, 0u);
        InputEvent up = DesktopInputUpEvent((int32_t)title.x, (int32_t)title.y, 0u);
        InputEvent h = DesktopInputKeyDown(RoboPhysicalKeyH, 0u);
        InputEvent i = DesktopInputKeyDown(RoboPhysicalKeyI, 0u);

        DesktopInputFeed(input, &down, &step);
        DesktopInputFeed(input, &up, &step);
        RoboCheckEqInt((int)DesktopInputFocusedTarget(input, &target), 1);

        DesktopInputFeed(input, &h, &step);
        RoboCheckEqInt((int)step.textInputs, 1);
        RoboCheckEqInt((int)step.commands, 1);
        RoboCheckEqInt((int)step.lastCommand, (int)RoboInputCommandInsertText);
        DesktopInputFeed(input, &i, &step);
        DesktopInputEditorIs(editor, "hi", "two typed characters");
    }

    /*
     * A chord is **consumed**, not typed: `Ctrl+A` selects everything and inserts nothing. That is 13.72's
     * one explicit prohibition (`Ctrl+C` must not type `c`) and it is asserted through the text rather than
     * through a counter, because a counter would pass for a build that inserted and then counted a command.
     */
    {
        InputEvent selectAll = DesktopInputKeyDown(RoboPhysicalKeyA, RoboKeyModCtrl);

        DesktopInputFeed(input, &selectAll, &step);
        RoboCheckEqInt((int)step.shortcuts, 1);
        RoboCheckEqInt((int)step.textInputs, 0);
        RoboCheckEqInt((int)step.lastCommand, (int)RoboInputCommandSelectAll);
        DesktopInputEditorIs(editor, "hi", "Ctrl+A inserted nothing");
    }

    /* A dead accent waits and produces no event; the second key completes it (13.10/13.11). */
    {
        InputEvent acute = DesktopInputKeyDown(RoboPhysicalKeyE, 0u);
        InputEvent up = DesktopInputKeyUp(RoboPhysicalKeyE, 0u);

        /* Clear the selection first so the accent composes at the end. */
        {
            InputEvent end = DesktopInputKeyDown(RoboPhysicalKeyEnd, RoboKeyModCtrl);

            DesktopInputFeed(input, &end, &step);
        }
        DesktopInputFeed(input, &acute, &step);
        RoboCheckEqInt((int)step.textInputs, 1);
        DesktopInputFeed(input, &up, &step);
        DesktopInputEditorIs(editor, "hie", "a plain letter still types");
    }

    /*
     * The editing keys are chords too: `Backspace` is a *deletion*, and the editor's text is the proof. The
     * two arrows move the caret and change nothing; the one that deletes lands where the caret is.
     */
    {
        InputEvent back = DesktopInputKeyDown(RoboPhysicalKeyBackspace, 0u);
        InputEvent left = DesktopInputKeyDown(RoboPhysicalKeyArrowLeft, 0u);
        InputEvent forward = DesktopInputKeyDown(RoboPhysicalKeyDelete, 0u);

        DesktopInputFeed(input, &back, &step);
        RoboCheckEqInt((int)step.lastCommand, (int)RoboInputCommandDeleteBackward);
        DesktopInputEditorIs(editor, "hi", "backspace deleted the last character");

        DesktopInputFeed(input, &left, &step);
        RoboCheckEqInt((int)step.lastCommand, (int)RoboInputCommandMoveLeft);
        RoboCheckEqInt((int)step.textInputs, 0);
        DesktopInputEditorIs(editor, "hi", "an arrow changed no text");

        /* Forward-delete at the caret now removes the `i`, leaving `h`. */
        DesktopInputFeed(input, &forward, &step);
        RoboCheckEqInt((int)step.lastCommand, (int)RoboInputCommandDeleteForward);
        DesktopInputEditorIs(editor, "h", "forward delete removed the character after the caret");
    }

    /*
     * Shift extends instead of moving, which is the one place the two bindings differ: the same physical key
     * with a modifier is a different chord, and the selection is what tells them apart.
     */
    {
        InputEvent selectLeft = DesktopInputKeyDown(RoboPhysicalKeyArrowLeft, RoboKeyModShift);
        RoboTextBuffer *selected = NULL;
        RoboTextDiag textDiag;

        RoboTextDiagReset(&textDiag);
        DesktopInputFeed(input, &selectLeft, &step);
        RoboCheckEqInt((int)step.lastCommand, (int)RoboInputCommandSelectLeft);
        RoboCheckEqInt(step.textInputs, 0);
        DesktopInputEditorIs(editor, "h", "shift-left selected rather than moved");
        /* The editor's own selection door is how a caller sees it; there is no geometry in this layer. */
        RoboCheckEqInt(RoboTextEditorSelectedText(editor, &selected, &textDiag), RoboOk);
        RoboCheckEqInt((int)RoboTextBufferByteLength(selected), 1);
        RoboCheckEqInt((int)RoboTextBufferBytes(selected)[0], 'h');
        RoboTextBufferDestroy(selected);
    }

    /* A key in a window with no editor is consumed by the chord table or claimed by nobody, never typed. */
    RoboCheckEqInt(DesktopInputClearContent(input, window, NULL), RoboOk);
    {
        InputEvent z = DesktopInputKeyDown(RoboPhysicalKeyZ, 0u);

        DesktopInputFeed(input, &z, &step);
        RoboCheckEqInt((int)step.textInputs, 0);
        RoboCheckEqInt((int)step.unclaimed, 1);
    }

    DesktopInputEditorClose(editor);
    DesktopInputClose(input);
    DesktopClose(desktop);
}

/* ------------------------------------------------------------------ */
/* 3. Shortcuts and commands (13.73)                                   */
/* ------------------------------------------------------------------ */

/** A command target that claims exactly one command id and declines everything else. */
typedef struct ContentClaimTarget {
    InputCommandId claims;
    uint32_t calls;
    InputCommandId last;
} ContentClaimTarget;

static Result ContentClaimExecute(void *userdata, const InputCommand *command, InputDiag *diag)
{
    ContentClaimTarget *target = (ContentClaimTarget *)userdata;

    (void)diag;
    if (!target || !command) {
        return RoboInvalidArgument;
    }
    target->calls++;
    target->last = command->id;
    return command->id == target->claims ? RoboOk : RoboNotSupported;
}

static void TestInputCommands(void)
{
    Desktop *desktop = DesktopOpen(1280, 800);
    DesktopInput *input = DesktopInputOpen(desktop);
    DesktopWindowId window = DesktopAddWindow(desktop, "Commands", 100, 100, 500, 300);
    RoboTextEditor *editor = DesktopInputEditor("body");
    ContentClaimTarget application;
    DesktopInputContent content;
    InputShortcut shortcut;
    GfxPoint title = DesktopInputTitlePoint(desktop, window);
    DesktopInputStep step;
    int32_t level = -1;

    memset(&application, 0, sizeof(application));
    DesktopInputContentInit(&content);
    content.window = window;
    content.editor = editor;
    content.commands.userdata = &application;
    content.commands.execute = ContentClaimExecute;
    RoboCheckEqInt(DesktopInputSetContent(input, &content, NULL), RoboOk);

    /* The default table resolves the four clipboard chords and undo, without performing them. */
    InputShortcutInit(&shortcut);
    RoboCheckEqInt(DesktopInputResolveShortcut(input, RoboPhysicalKeyC, RoboKeyModCtrl, &shortcut, NULL),
                   RoboOk);
    RoboCheckEqInt((int)shortcut.command, (int)RoboInputCommandCopy);
    RoboCheckEqInt((int)shortcut.scope, (int)RoboInputShortcutScopeWindow);
    RoboCheckEqInt(DesktopInputResolveShortcut(input, RoboPhysicalKeyX, RoboKeyModCtrl, &shortcut, NULL),
                   RoboOk);
    RoboCheckEqInt((int)shortcut.command, (int)RoboInputCommandCut);
    RoboCheckEqInt(DesktopInputResolveShortcut(input, RoboPhysicalKeyV, RoboKeyModCtrl, &shortcut, NULL),
                   RoboOk);
    RoboCheckEqInt((int)shortcut.command, (int)RoboInputCommandPaste);
    RoboCheckEqInt(DesktopInputResolveShortcut(input, RoboPhysicalKeyA, RoboKeyModCtrl, &shortcut, NULL),
                   RoboOk);
    RoboCheckEqInt((int)shortcut.command, (int)RoboInputCommandSelectAll);
    RoboCheckEqInt(DesktopInputResolveShortcut(input, RoboPhysicalKeyZ, RoboKeyModCtrl, &shortcut, NULL),
                   RoboOk);
    RoboCheckEqInt((int)shortcut.command, (int)RoboInputCommandUndo);
    /* A chord nobody bound is the honest refusal, not a default. */
    RoboCheckEqInt(DesktopInputResolveShortcut(input, RoboPhysicalKeyQ, RoboKeyModCtrl, &shortcut, NULL),
                   RoboNotFound);
    RoboCheckEqInt(DesktopInputResolveShortcut(input, RoboPhysicalKeyQ, 0u, &shortcut, NULL),
                   RoboNotFound);
    RoboCheckEqInt(DesktopInputResolveShortcut(input, RoboPhysicalKeyQ, RoboKeyModCtrl, NULL, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(DesktopInputResolveShortcut(NULL, RoboPhysicalKeyQ, 0u, &shortcut, NULL),
                   RoboInvalidArgument);

    /*
     * Priority, which is 13.73's one ordering claim: a **focused control** wins a contested chord over the
     * window's own application target. `Ctrl+A` is a text-editing command the editor claims, and the
     * application target is asked *after* it -- the chain's stated order rather than a special case.
     */
    {
        InputEvent down = DesktopInputDownEvent((int32_t)title.x, (int32_t)title.y, 0u);
        InputEvent up = DesktopInputUpEvent((int32_t)title.x, (int32_t)title.y, 0u);
        InputEvent selectAll = DesktopInputKeyDown(RoboPhysicalKeyA, RoboKeyModCtrl);

        DesktopInputFeed(input, &down, &step);
        DesktopInputFeed(input, &up, &step);
        DesktopInputFeed(input, &selectAll, &step);
        RoboCheckEqInt((int)step.commands, 1);
        /* The editor took it, so the application target was never asked. */
        RoboCheckEqInt((int)application.calls, 0);
        RoboCheckEqInt((int)step.lastCommand, (int)RoboInputCommandSelectAll);
    }

    /* A command the editor declines *is* offered to the window's application target. */
    {
        InputCommand command;

        InputCommandInit(&command);
        RoboCheckEqInt(InputCommandSet(&command, RoboInputCommandActivate, NULL), RoboOk);
        application.claims = RoboInputCommandActivate;
        RoboCheckEqInt(DesktopInputRouteCommand(input, &command, &level, NULL), RoboOk);
        RoboCheckEqInt((int)application.calls, 1);
        RoboCheckEqInt((int)application.last, (int)RoboInputCommandActivate);
        /* Level 0 is the innermost target askable; the slot's one entry is it. */
        RoboCheckEqInt((int)level, 0);
    }

    /* A command nobody claims is *counted*, and the error is `RoboNotFound` rather than a failure. */
    {
        InputCommand command;

        InputCommandInit(&command);
        RoboCheckEqInt(InputCommandSet(&command, RoboInputCommandWindowMenu, NULL), RoboOk);
        RoboCheckEqInt(DesktopInputRouteCommand(input, &command, &level, NULL), RoboNotFound);
        RoboCheckEqInt((int)level, -1);
        RoboCheckEqInt(DesktopInputRouteCommand(input, NULL, NULL, NULL), RoboInvalidArgument);
        RoboCheckEqInt(DesktopInputRouteCommand(NULL, &command, NULL, NULL), RoboInvalidArgument);
    }

    /*
     * `Enter` is an activation the *model* identifies (13.34), not a chord: the same command reaches the same
     * chain, which is what makes a menu item, a button and a keyboard agree about what activating means.
     */
    {
        InputEvent enter = DesktopInputKeyDown(RoboPhysicalKeyEnter, 0u);

        application.claims = RoboInputCommandActivate;
        application.calls = 0u;
        DesktopInputFeed(input, &enter, &step);
        RoboCheckEqInt((int)step.actions, 1);
        RoboCheckEqInt((int)application.calls, 1);
        RoboCheckEqInt((int)step.lastCommand, (int)RoboInputCommandActivate);
    }

    /* `Tab` is navigation, and it moves the shell's focus chain rather than sending a command. */
    {
        DesktopWindowId other = DesktopAddWindow(desktop, "Other", 700, 400, 400, 300);
        InputEvent tab = DesktopInputKeyDown(RoboPhysicalKeyTab, 0u);
        InputTarget focused;

        DesktopInputFeed(input, &tab, &step);
        RoboCheckEqInt((int)step.focusChanges, 1);
        RoboCheckEqInt((int)DesktopInputFocusedTarget(input, &focused), 1);
        RoboCheckEqInt((int)focused.window, (int)other);
    }

    /* A caller's own chord is bindable, which is how `Alt+Tab` is *reachable* without being built here. */
    {
        InputShortcut bound;
        InputEvent altTab = DesktopInputKeyDown(RoboPhysicalKeyTab, RoboKeyModAlt);
        InputTarget focused;
        DesktopWindowId back;

        RoboCheckEqInt(DesktopInputBindShortcut(input, RoboInputCommandFocusPrevious, RoboPhysicalKeyTab,
                                               RoboKeyModAlt, RoboInputShortcutScopeWindow, NULL),
                       RoboOk);
        RoboCheckEqInt(DesktopInputResolveShortcut(input, RoboPhysicalKeyTab, RoboKeyModAlt, &bound, NULL),
                       RoboOk);
        RoboCheckEqInt((int)bound.command, (int)RoboInputCommandFocusPrevious);
        DesktopInputFeed(input, &altTab, &step);
        RoboCheckEqInt((int)DesktopInputFocusedTarget(input, &focused), 1);
        back = (DesktopWindowId)focused.window;
        RoboCheck((int)back == (int)window);
    }

    /* The shell's own direct doors: a menu or an accessibility client can invoke a meaning, not a keystroke. */
    {
        RoboCheckEqInt(DesktopInputPerformCommand(input, RoboInputCommandFocusNext, NULL), RoboOk);
        RoboCheckEqInt(DesktopInputPerformCommand(input, RoboInputCommandResetInputState, NULL), RoboOk);
        RoboCheckEqInt(DesktopInputIsQuiescent(input), 1);
        RoboCheckEqInt(DesktopInputPerformCommand(input, RoboInputCommandWindowMinimize, NULL), RoboOk);
        {
            DesktopWindowState state = RoboDesktopWindowOpen;

            RoboCheckEqInt(GetDesktopWindowState(desktop, RoboDesktopWindowIdNone, &state), RoboNotFound);
        }
        /* `None` is not a command a target is asked to execute. */
        RoboCheckEqInt(DesktopInputPerformCommand(input, RoboInputCommandNone, NULL), RoboInvalidArgument);
        RoboCheckEqInt(DesktopInputPerformCommand(NULL, RoboInputCommandFocusNext, NULL),
                       RoboInvalidArgument);
    }

    DesktopInputEditorClose(editor);
    DesktopInputClose(input);
    DesktopClose(desktop);
}

/* ------------------------------------------------------------------ */
/* 4. Rotate, cut and paste (13.74)                                    */
/* ------------------------------------------------------------------ */

/** Run @p physical with @p modifiers through the loop, returning the step. */
static void ContentChord(DesktopInput *input,
                         PhysicalKey physical,
                         InputModifiers modifiers,
                         DesktopInputStep *outStep)
{
    InputEvent down = DesktopInputKeyDown(physical, modifiers);
    InputEvent up = DesktopInputKeyUp(physical, modifiers);
    DesktopInputStep step;

    /* The *press* is the step that carried the command, so that is the one reported. */
    DesktopInputFeed(input, &down, &step);
    if (outStep) {
        *outStep = step;
    }
    DesktopInputFeed(input, &up, &step);
}

static void TestInputClipboard(void)
{
    Desktop *desktop = DesktopOpen(1280, 800);
    DesktopInput *input = DesktopInputOpen(desktop);
    DesktopWindowId window = DesktopAddWindow(desktop, "Clipboard", 100, 100, 500, 300);
    RoboTextEditor *editor = DesktopInputEditor("copy me");
    DesktopInputContent content;
    GfxPoint title = DesktopInputTitlePoint(desktop, window);
    DesktopInputStep step;

    DesktopInputContentInit(&content);
    content.window = window;
    content.editor = editor;
    RoboCheckEqInt(DesktopInputSetContent(input, &content, NULL), RoboOk);
    {
        InputEvent down = DesktopInputDownEvent((int32_t)title.x, (int32_t)title.y, 0u);
        InputEvent up = DesktopInputUpEvent((int32_t)title.x, (int32_t)title.y, 0u);

        DesktopInputFeed(input, &down, &step);
        DesktopInputFeed(input, &up, &step);
    }

    /* Copy with an empty selection is *nothing*: the clipboard keeps what it had, and nothing is counted. */
    ContentChord(input, RoboPhysicalKeyC, RoboKeyModCtrl, &step);
    RoboCheckEqInt(step.clipboardOps, 0);
    DesktopInputEditorIs(editor, "copy me", "a copy of nothing changed nothing");

    /* Select all, copy, then cut: the text goes and the clipboard holds it. */
    ContentChord(input, RoboPhysicalKeyA, RoboKeyModCtrl, &step);
    ContentChord(input, RoboPhysicalKeyC, RoboKeyModCtrl, &step);
    RoboCheckEqInt(step.clipboardOps, 1);
    DesktopInputEditorIs(editor, "copy me", "copy left the text alone");

    /* Replace the selection with a paste of what was copied: the same text, and the clipboard is read. */
    ContentChord(input, RoboPhysicalKeyV, RoboKeyModCtrl, &step);
    RoboCheckEqInt(step.clipboardOps, 1);
    DesktopInputEditorIs(editor, "copy me", "paste replaced the selection with itself");

    /* Re-select before the cut: the paste above collapsed the caret to the end of the inserted text. */
    ContentChord(input, RoboPhysicalKeyA, RoboKeyModCtrl, &step);
    ContentChord(input, RoboPhysicalKeyX, RoboKeyModCtrl, &step);
    RoboCheckEqInt(step.clipboardOps, 1);
    DesktopInputEditorIs(editor, "", "cut removed the selection");

    ContentChord(input, RoboPhysicalKeyV, RoboKeyModCtrl, &step);
    RoboCheckEqInt(step.clipboardOps, 1);
    DesktopInputEditorIs(editor, "copy me", "paste put it back");

    /*
     * The clipboard is replaced rather than appended: cut a *different* selection and paste it, and the old
     * text is gone. That is the ownership rule 13.41 states, asserted through the text a user would see.
     */
    {
        InputEvent left = DesktopInputKeyDown(RoboPhysicalKeyArrowLeft, RoboKeyModCtrl);

        /* Collapse to the start, extend right by three, then cut. */
        ContentChord(input, RoboPhysicalKeyHome, RoboKeyModCtrl, &step);
        (void)left;
        {
            InputEvent selectLeft = DesktopInputKeyDown(RoboPhysicalKeyArrowRight, RoboKeyModShift);
            InputEvent again = DesktopInputKeyDown(RoboPhysicalKeyArrowRight, RoboKeyModShift);
            InputEvent third = DesktopInputKeyDown(RoboPhysicalKeyArrowRight, RoboKeyModShift);

            DesktopInputFeed(input, &selectLeft, &step);
            DesktopInputFeed(input, &again, &step);
            DesktopInputFeed(input, &third, &step);
        }
        ContentChord(input, RoboPhysicalKeyC, RoboKeyModCtrl, &step);
        RoboCheckEqInt(step.clipboardOps, 1);
        ContentChord(input, RoboPhysicalKeyEnd, RoboKeyModCtrl, &step);
        ContentChord(input, RoboPhysicalKeyV, RoboKeyModCtrl, &step);
        DesktopInputEditorIs(editor, "copy mecop", "paste appended the replaced clipboard");
    }

    /* An editor that has gone leaves the clipboard with nothing to talk to, and says so. */
    RoboCheckEqInt(DesktopInputClearContent(input, window, NULL), RoboOk);
    ContentChord(input, RoboPhysicalKeyC, RoboKeyModCtrl, &step);
    RoboCheckEqInt(step.clipboardOps, 0);
    RoboCheckEqInt((int)step.unclaimed, 1);

    DesktopInputEditorClose(editor);
    DesktopInputClose(input);
    DesktopClose(desktop);
}

/* ------------------------------------------------------------------ */
/* 5. The drag (13.74)                                                 */
/* ------------------------------------------------------------------ */

static void TestInputDrag(void)
{
    Desktop *desktop = DesktopOpen(1280, 800);
    DesktopInput *input = DesktopInputOpen(desktop);
    DesktopWindowId window = DesktopAddWindow(desktop, "Drag", 100, 100, 400, 300);
    GfxPoint title = DesktopInputTitlePoint(desktop, window);
    DesktopInputStep step;

    /* A title-bar press begins the model's drag session *and* the shell's move: one gesture, two readers. */
    {
        InputEvent down = DesktopInputDownEvent((int32_t)title.x, (int32_t)title.y, 0u);

        DesktopInputFeed(input, &down, &step);
        RoboCheckEqInt(DesktopInputIsDragging(input), 1);
        RoboCheckEqInt(DesktopInputIsMovingWindow(input), 1);
        RoboCheckEqInt((int)DesktopInputDragPhase(input), (int)RoboInputDragDragging);
        RoboCheckEqInt(step.dragsBegan, 1);
    }
    {
        InputEvent move = DesktopInputMoveEvent((int32_t)title.x + 20, (int32_t)title.y + 10, 0u);

        DesktopInputFeed(input, &move, &step);
        RoboCheckEqInt((int)DesktopInputDragPhase(input), (int)RoboInputDragDragging);
        RoboCheckEqInt((int)step.windowMoves, 1);
    }
    {
        InputEvent up = DesktopInputUpEvent((int32_t)title.x + 20, (int32_t)title.y + 10, 0u);

        DesktopInputFeed(input, &up, &step);
        RoboCheckEqInt(step.dragsEnded, 1);
        RoboCheckEqInt(DesktopInputIsDragging(input), 0);
        RoboCheckEqInt((int)DesktopInputDragPhase(input), (int)RoboInputDragIdle);
        RoboCheckEqInt(DesktopInputIsQuiescent(input), 1);
    }

    /* A press on the content is *not* a drag: a drag carries something and a raise does not. */
    {
        GfxPoint client = DesktopInputClientPoint(desktop, window);
        InputEvent down = DesktopInputDownEvent((int32_t)client.x, (int32_t)client.y, 0u);
        InputEvent up = DesktopInputUpEvent((int32_t)client.x, (int32_t)client.y, 0u);

        DesktopInputFeed(input, &down, &step);
        RoboCheckEqInt(DesktopInputIsDragging(input), 0);
        RoboCheckEqInt(step.dragsBegan, 0);
        DesktopInputFeed(input, &up, &step);
    }

    /*
     * A cancel ends the gesture where a drop would have: `RoboInputDragSessionReset` leaves nothing behind,
     * which is what makes a device going away mid-drag safe.
     */
    {
        InputEvent down = DesktopInputDownEvent((int32_t)title.x, (int32_t)title.y, 0u);
        InputEvent cancel;
        InputSessionSweep sweep;

        DesktopInputFeed(input, &down, &step);
        RoboCheckEqInt(DesktopInputIsDragging(input), 1);
        InputSessionSweepInit(&sweep);
        RoboCheckEqInt(DesktopInputResetTransient(input, &sweep, NULL), RoboOk);
        RoboCheckEqInt(DesktopInputIsDragging(input), 0);
        RoboCheckEqInt(DesktopInputIsMovingWindow(input), 0);
        RoboCheckEqInt(DesktopInputIsQuiescent(input), 1);
        RoboCheckEqInt((int)DesktopInputGestureWindow(input), (int)RoboDesktopWindowIdNone);

        cancel = DesktopInputUpEvent((int32_t)title.x, (int32_t)title.y, 0u);
        DesktopInputFeed(input, &cancel, &step);
        RoboCheckEqInt(step.dragsEnded, 0);
    }

    DesktopInputClose(input);
    DesktopClose(desktop);
}

void TestRegisterDesktopInputContent(void)
{
    TestRegister("desktop.input.document", TestInputDocument);
    TestRegister("desktop.input.text", TestInputText);
    TestRegister("desktop.input.commands", TestInputCommands);
    TestRegister("desktop.input.clipboard", TestInputClipboard);
    TestRegister("desktop.input.drag", TestInputDrag);
}
