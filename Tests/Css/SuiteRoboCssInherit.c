/*
 * RoboCSS inheritance model (Phase 9.7) — css.inherit.*
 *
 * The model without the resolver: how a value is classified as a CSS-wide
 * keyword, how provenance is named, how a resolved style is looked through, and
 * how the value copy underneath all of it behaves.
 *
 * The keyword table is the part worth pinning hardest, because the *negative*
 * cases are the ones that matter. Recognising `inherit` is easy; refusing to
 * recognise `inherit inherit`, `calc(inherit)` or a bare `red` is what keeps a
 * resolution from inventing a value the author never wrote, and a table that
 * matched on "contains the word inherit" would pass every positive test here.
 */

#include "RoboCssTestSupport.h"

/* ------------------------------------------------------------------ */
/* Keyword classification                                              */
/* ------------------------------------------------------------------ */

/** Classify the value a declaration's text carries in @p text. */
static CssWideKeyword CssKeywordOf(const char *text)
{
    CssDeclarationList list = RoboCssCleared;
    CssError error = RoboCssCleared;
    CssWideKeyword keyword = RoboCssWideKeywordNone;

    /* Parsed, not guessed: a keyword is recognised on the real value tree, so
     * the test cannot pass by reading bytes the way a wrong implementation
     * might. */
    RoboCheckEqInt(CssParseDeclarations(text, &list, &error), RoboOk);
    if (CssDeclarationListCount(&list) == 1u) {
        keyword = CssWideKeywordOf(
            CssDeclarationValue(CssDeclarationListAt(&list, 0u)));
    }
    CssDeclarationListDispose(&list);
    return keyword;
}

static void TestCssInheritWideKeywords(void)
{
    CssFixture();

    RoboCheckEqInt(CssKeywordOf("color: inherit"), RoboCssWideKeywordInherit);
    RoboCheckEqInt(CssKeywordOf("color: initial"), RoboCssWideKeywordInitial);
    RoboCheckEqInt(CssKeywordOf("color: unset"), RoboCssWideKeywordUnset);
    RoboCheckEqInt(CssKeywordOf("color: revert"), RoboCssWideKeywordRevert);
    /* Case is irrelevant for a keyword, as it is for every CSS identifier. */
    RoboCheckEqInt(CssKeywordOf("color: INHERIT"), RoboCssWideKeywordInherit);
    RoboCheckEqInt(CssKeywordOf("color: InHeRiT"), RoboCssWideKeywordInherit);
    /* An escape changes which bytes spell a name, never which name it is. */
    RoboCheckEqInt(CssKeywordOf("color: \\69 nherit"), RoboCssWideKeywordInherit);

    /* And now the half that keeps a resolution honest: everything that is not a
     * single identifier is not a keyword. */
    RoboCheckEqInt(CssKeywordOf("color: red"), RoboCssWideKeywordNone);
    RoboCheckEqInt(CssKeywordOf("color: auto"), RoboCssWideKeywordNone);
    RoboCheckEqInt(CssKeywordOf("color: inherit inherit"),
                      RoboCssWideKeywordNone);
    RoboCheckEqInt(CssKeywordOf("color: red inherit"), RoboCssWideKeywordNone);
    RoboCheckEqInt(CssKeywordOf("color: calc(inherit)"), RoboCssWideKeywordNone);
    RoboCheckEqInt(CssKeywordOf("color: var(--x)"), RoboCssWideKeywordNone);
    RoboCheckEqInt(CssKeywordOf("color: \"inherit\""), RoboCssWideKeywordNone);
    RoboCheckEqInt(CssKeywordOf("color: 12px"), RoboCssWideKeywordNone);
    RoboCheckEqInt(CssKeywordOf("color: inherit!"), RoboCssWideKeywordNone);
    /* `inherit` before `!important` is the keyword *with* importance, which is
     * a surprising spelling that must keep working: the flag is taken off the
     * value before a keyword is looked for. */
    RoboCheckEqInt(CssKeywordOf("color: inherit !important"),
                      RoboCssWideKeywordInherit);
    /* An empty value is not a keyword — and not a value either. */
    RoboCheckEqInt(CssKeywordOf("color: "), RoboCssWideKeywordNone);

    /* A hand-built value is a shape the parser never produces, so the API has to
     * be told what it does with one rather than left to guess. */
    RoboCheckEqInt(CssWideKeywordOf(NULL), RoboCssWideKeywordNone);
    {
        CssValue empty = RoboCssCleared;

        CssValueInit(&empty);
        RoboCheckEqInt(CssWideKeywordOf(&empty), RoboCssWideKeywordNone);
        CssValueDispose(&empty);
    }
    {
        /* A bare IDENT token — the shape a hand-built declaration carries. The
         * value accessor reads the token's own text, so the bytes have to be
         * real: a token made with no source is not a token and is refused. */
        CssValue token = RoboCssCleared;
        CssToken built = RoboCssCleared;
        CssSource source = RoboCssCleared;

        CssSourceOfLiteral(&source, "initial");
        CssTokenOfText(&built, RoboCssTokenIdent, &source, "initial");
        RoboCheckEqInt(CssValueMakeToken(&token, &built, NULL), RoboOk);
        RoboCheckEqInt(CssWideKeywordOf(&token),
                          RoboCssWideKeywordInitial);
        CssValueDispose(&token);
    }
}

static void TestCssInheritKeywordNames(void)
{
    RoboCheckStrEq(CssWideKeywordName(RoboCssWideKeywordNone), "none");
    RoboCheckStrEq(CssWideKeywordName(RoboCssWideKeywordInherit), "inherit");
    RoboCheckStrEq(CssWideKeywordName(RoboCssWideKeywordInitial), "initial");
    RoboCheckStrEq(CssWideKeywordName(RoboCssWideKeywordUnset), "unset");
    RoboCheckStrEq(CssWideKeywordName(RoboCssWideKeywordRevert), "revert");
    RoboCheckStrEq(CssWideKeywordName((CssWideKeyword)77), "unknown");
    RoboCheckEqInt(CssWideKeywordIsKnown(RoboCssWideKeywordNone), 1);
    RoboCheckEqInt(CssWideKeywordIsKnown(RoboCssWideKeywordRevert), 1);
    RoboCheckEqInt(CssWideKeywordIsKnown((CssWideKeyword)77), 0);
    RoboCheckEqInt(RoboCssWideKeywordCount, 5);
}

static void TestCssInheritSourceNames(void)
{
    RoboCheckStrEq(CssInheritedSourceName(RoboCssInheritedSourceNone),
                      "none");
    RoboCheckStrEq(CssInheritedSourceName(RoboCssInheritedSourceDeclared),
                      "declared");
    RoboCheckStrEq(CssInheritedSourceName(RoboCssInheritedSourceParent),
                      "parent");
    RoboCheckStrEq(CssInheritedSourceName(RoboCssInheritedSourceDefault),
                      "default");
    RoboCheckStrEq(CssInheritedSourceName(RoboCssInheritedSourceDeferred),
                      "deferred");
    RoboCheckStrEq(CssInheritedSourceName((CssInheritedSource)77),
                      "unknown");
    RoboCheckEqInt(CssInheritedSourceIsKnown(RoboCssInheritedSourceNone),
                      1);
    RoboCheckEqInt(CssInheritedSourceIsKnown(RoboCssInheritedSourceDeferred),
                      1);
    RoboCheckEqInt(CssInheritedSourceIsKnown((CssInheritedSource)77),
                      0);
    RoboCheckEqInt(RoboCssInheritedSourceCount, 5);
}

/* ------------------------------------------------------------------ */
/* The resolved style                                                  */
/* ------------------------------------------------------------------ */

static void TestCssInheritStyleLifecycle(void)
{
    CssInheritedStyle style = RoboCssCleared;
    CssInheritedStyle other = RoboCssCleared;

    CssFixture();
    CssInheritedStyleInit(&style);
    RoboCheckEqInt((int)CssInheritedStyleCount(&style), 0);
    RoboCheck(CssInheritedStyleAt(&style, 0u) == NULL);
    RoboCheck(CssInheritedStyleFind(&style, CssSliceFromText("color")) ==
               NULL);
    RoboCheckEqInt(CssInheritedStyleHas(&style,
                                                  CssSliceFromText("color")),
                      0);
    RoboCheckEqInt(CssInheritedStyleElement(&style),
                      RoboCssElementRefNone);
    RoboCheckEqInt(CssInheritedStyleHasParent(&style), 0);
    /* An empty style is a *complete* answer, not an incomplete one: nothing was
     * asked and nothing was refused. */
    RoboCheckEqInt(CssInheritedStyleIsComplete(&style), 1);

    /* Every accessor tolerates NULL the same way the rest of the model does. */
    RoboCheckEqInt((int)CssInheritedStyleCount(NULL), 0);
    RoboCheck(CssInheritedStyleAt(NULL, 0u) == NULL);
    RoboCheck(CssInheritedStyleFind(NULL, CssSliceFromText("x")) == NULL);
    RoboCheckEqInt(CssInheritedStyleIsComplete(NULL), 0);
    RoboCheckEqInt(CssInheritedStyleElement(NULL), RoboCssElementRefNone);
    RoboCheckEqInt(CssInheritedStyleHasParent(NULL), 0);
    RoboCheck(CssInheritedPropertyName(NULL) .bytes == NULL);
    RoboCheck(CssInheritedPropertyValue(NULL) == NULL);

    /* Disposal is idempotent, and a record left cleared after it is reusable —
     * the record-lifetime rule every RoboCSS type follows. */
    CssInheritedStyleDispose(&style);
    CssInheritedStyleDispose(&style);
    CssInheritedStyleDispose(NULL);
    RoboCheckEqInt((int)CssInheritedStyleCount(&style), 0);

    /* Moving one by value, which is what the context door does, must not
     * double-free: the source is re-initialised after the move. */
    other = style;
    CssInheritedStyleInit(&style);
    CssInheritedStyleDispose(&other);
    CssInheritedStyleDispose(&style);
}

/* ------------------------------------------------------------------ */
/* The value copy                                                      */
/* ------------------------------------------------------------------ */

static void TestCssInheritValueCopy(void)
{
    CssValue original = RoboCssCleared;
    CssValue copy = RoboCssCleared;
    char shape[256];

    CssFixture();

    /* A four-level tree: list -> function -> block -> token. Deep enough that a
     * shallow copy would be visible in the shape. */
    {
        CssValue function = RoboCssCleared;
        CssValue block = RoboCssCleared;
        CssValue leaf = RoboCssCleared;
        CssToken token = RoboCssCleared;
        CssSource source = RoboCssCleared;

        CssSourceOfLiteral(&source, "1");
        RoboCheckEqInt(CssValueMakeList(&original, NULL), RoboOk);
        RoboCheckEqInt(CssValueMakeFunction(&function,
                                                      CssSliceFromText("calc"),
                                                      CssRangeAt(0u), NULL),
                          RoboOk);
        RoboCheckEqInt(CssValueMakeBlock(&block, RoboCssTokenLeftSquare,
                                                   CssRangeAt(0u), NULL),
                          RoboOk);
        CssTokenOfText(&token, RoboCssTokenNumber, &source, "1");
        RoboCheckEqInt(CssValueMakeToken(&leaf, &token, NULL), RoboOk);
        RoboCheckEqInt(CssValueAppend(&block, &leaf, NULL), RoboOk);
        RoboCheckEqInt(CssValueAppend(&function, &block, NULL), RoboOk);
        RoboCheckEqInt(CssValueAppend(&original, &function, NULL), RoboOk);
    }
    RoboCheckEqInt((int)CssValueDepth(&original), 4);
    RoboCheckEqInt((int)CssValueTreeCount(&original), 4);

    RoboCheckEqInt(CssValueCopy(&copy, &original, NULL), RoboOk);
    RoboCheckEqInt((int)CssValueDepth(&copy), 4);
    RoboCheckEqInt((int)CssValueTreeCount(&copy), 4);
    CssShape(&copy, shape, sizeof(shape));
    /* The token renders as its own bytes — the shape writer prints what a token
     * covers, not its type — so a NUMBER token over `1` is `1` here. */
    RoboCheckStrEq(shape, "calc([1])");

    /* The copy is *independent*: emptying the copy's children must leave the
     * original intact. This is the property that makes a child's inherited value
     * safe from the parent's disposal, and a shallow copy would fail it. */
    RoboCheckEqInt(CssValueCopy(&copy, &original, NULL), RoboOk);
    {
        /* Reach into the copy the way a mutation would, and prove the original
         * did not move. */
        CssValue *copiedChild = &copy.children.items[0];

        RoboCheckEqInt(copiedChild != &original.children.items[0], 1);
        RoboCheckEqInt((int)CssValueChildCount(copiedChild), 1);
        CssValueClearChildren(copiedChild);
        RoboCheckEqInt((int)CssValueChildCount(copiedChild), 0);
    }
    CssShape(&original, shape, sizeof(shape));
    RoboCheckStrEq(shape, "calc([1])");

    /* Disposing the copy leaves the original alone, and the other way round. */
    CssValueDispose(&copy);
    RoboCheckEqInt(CssValueIsEmpty(&copy), 1);
    CssShape(&original, shape, sizeof(shape));
    RoboCheckStrEq(shape, "calc([1])");
    RoboCheckEqInt(CssValueCopy(&copy, &original, NULL), RoboOk);
    CssValueDispose(&original);
    RoboCheckEqInt((int)CssValueDepth(&copy), 4);
    CssValueDispose(&copy);

    /* Recopying into a value that already holds a tree replaces it rather than
     * leaking it — the rule every RoboCSS "set" call follows. */
    RoboCheckEqInt(CssValueMakeList(&original, NULL), RoboOk);
    RoboCheckEqInt(CssValueCopy(&copy, &original, NULL), RoboOk);
    {
        CssValue leaf = RoboCssCleared;
        CssToken token = RoboCssCleared;
        CssSource source = RoboCssCleared;

        CssSourceOfLiteral(&source, "a");
        CssTokenOfText(&token, RoboCssTokenIdent, &source, "a");
        RoboCheckEqInt(CssValueMakeToken(&leaf, &token, NULL), RoboOk);
        RoboCheckEqInt(CssValueAppend(&original, &leaf, NULL), RoboOk);
    }
    RoboCheckEqInt(CssValueCopy(&copy, &original, NULL), RoboOk);
    RoboCheckEqInt((int)CssValueTreeCount(&copy), 2);

    /* The argument contract. */
    RoboCheckEqInt(CssValueCopy(NULL, &original, NULL), RoboInvalidArgument);
    RoboCheckEqInt(CssValueCopy(&copy, NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(CssValueCopy(&copy, &copy, NULL), RoboInvalidArgument);
    CssValueDispose(&original);
    CssValueDispose(&copy);
}

static void TestCssInheritValueCopyBalances(void)
{
    int enabled;
    long baseline;

    CssFixture();
    enabled = CssAllocatorEnsureEnabled();
    baseline = MemDebugLiveCount();

    {
        CssValue list = RoboCssCleared;
        CssValue copy = RoboCssCleared;
        CssSource source = RoboCssCleared;
        size_t index;

        CssSourceOfLiteral(&source, "a");
        RoboCheckEqInt(CssValueMakeList(&list, NULL), RoboOk);
        for (index = 0u; index < 32u; index++) {
            CssValue leaf = RoboCssCleared;
            CssToken token = RoboCssCleared;

            CssTokenOfText(&token, RoboCssTokenIdent, &source, "a");
            RoboCheckEqInt(CssValueMakeToken(&leaf, &token, NULL), RoboOk);
            RoboCheckEqInt(CssValueAppend(&list, &leaf, NULL), RoboOk);
        }
        /* Copy over and over: each copy disposes the last, so the live count
         * must not creep. */
        for (index = 0u; index < 50u; index++) {
            RoboCheckEqInt(CssValueCopy(&copy, &list, NULL), RoboOk);
        }
        RoboCheckEqInt((int)CssValueTreeCount(&copy), 33);
        CssValueDispose(&copy);
        CssValueDispose(&list);
    }

    RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);
    CssAllocatorRestore(enabled);
}

void TestRegisterCssInherit(void);

void TestRegisterCssInherit(void)
{
    TestRegister("css.inherit.wide_keywords", TestCssInheritWideKeywords);
    TestRegister("css.inherit.keyword_names", TestCssInheritKeywordNames);
    TestRegister("css.inherit.source_names", TestCssInheritSourceNames);
    TestRegister("css.inherit.style_lifecycle", TestCssInheritStyleLifecycle);
    TestRegister("css.inherit.value_copy", TestCssInheritValueCopy);
    TestRegister("css.inherit.value_copy_balances",
                       TestCssInheritValueCopyBalances);
}
