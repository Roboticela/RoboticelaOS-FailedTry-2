/*
 * RoboCSS Phase 9.1 component-value suite:
 * the four kinds, nesting, and the move-in ownership rule.
 */

#include "RoboCssTestSupport.h"

static void TestCssValueKinds(void)
{
    CssValueKind kind;

    for (kind = RoboCssValueKindNone;
         kind < (CssValueKind)RoboCssValueKindCount;
         kind = (CssValueKind)(kind + 1)) {
        const char *name = CssValueKindName(kind);

        RoboCheck(CssValueKindIsKnown(kind));
        RoboCheck(name != NULL);
        RoboCheck(strcmp(name, "unknown") != 0);
    }
    RoboCheck(!CssValueKindIsKnown((CssValueKind)99));
    RoboCheckStrEq(CssValueKindName((CssValueKind)99), "unknown");

    /* A token is a leaf; the other three hold children. */
    RoboCheck(!CssValueKindHoldsChildren(RoboCssValueKindNone));
    RoboCheck(!CssValueKindHoldsChildren(RoboCssValueKindToken));
    RoboCheck(CssValueKindHoldsChildren(RoboCssValueKindFunction));
    RoboCheck(CssValueKindHoldsChildren(RoboCssValueKindBlock));
    RoboCheck(CssValueKindHoldsChildren(RoboCssValueKindList));
}

static void TestCssValueToken(void)
{
    CssSource source = RoboCssCleared;
    CssValue value = RoboCssCleared;
    CssToken token = RoboCssCleared;

    CssFixture();
    CssSourceOfString(&source, "red");
    CssValueInit(&value);
    RoboCheck(CssValueIsEmpty(&value));
    RoboCheckEqInt(value.kind, RoboCssValueKindNone);
    RoboCheckEqInt(CssValueChildCount(&value), 0u);
    RoboCheckEqInt(CssValueDepth(&value), 0u);
    RoboCheckEqInt(CssValueTreeCount(&value), 0u);

    RoboCheckEqInt(CssTokenMake(&token, RoboCssTokenIdent, &source,
                                          CssRangeSpan(0u, 3u), NULL),
                      RoboOk);
    RoboCheckEqInt(CssValueMakeToken(&value, &token, NULL), RoboOk);
    RoboCheck(!CssValueIsEmpty(&value));
    RoboCheckEqInt(value.kind, RoboCssValueKindToken);
    RoboCheck(CssSliceEqualsAscii(CssValueTokenText(&value), "red"));
    RoboCheckEqInt(CssValueToken(&value).type, RoboCssTokenIdent);
    RoboCheckEqInt(CssValueRange(&value).begin, 0u);
    RoboCheckEqInt(CssValueRange(&value).end, 3u);
    RoboCheckEqInt(CssValueDepth(&value), 1u);
    RoboCheckEqInt(CssValueTreeCount(&value), 1u);
    /* A token value has no name and no children, and says so rather than
     * inventing either. */
    RoboCheck(CssSliceIsEmpty(CssValueName(&value)));
    RoboCheckEqInt(CssValueChildCount(&value), 0u);

    RoboCheckEqInt(CssValueMakeToken(&value, NULL, NULL),
                      RoboInvalidArgument);
    CssTokenInit(&token); /* the NONE token is a known type, so it is legal */
    RoboCheckEqInt(CssValueMakeToken(&value, &token, NULL), RoboOk);
    RoboCheckEqInt(value.kind, RoboCssValueKindToken);
    RoboCheckEqInt(value.token.type, RoboCssTokenNone);

    CssValueDispose(&value);
    RoboCheck(CssValueIsEmpty(&value));
    RoboCheckEqInt(CssValueMakeToken(NULL, &token, NULL),
                      RoboInvalidArgument);
    CssSourceDispose(&source);
}

static void TestCssValueFunctionAndBlock(void)
{
    CssSource source = RoboCssCleared;
    CssValue function = RoboCssCleared;
    CssValue block = RoboCssCleared;

    CssFixture();
    CssSourceOfString(&source, "rgb(1,2,3)");
    CssValueInit(&function);
    CssValueInit(&block);

    RoboCheckEqInt(CssValueMakeFunction(&function,
                                                   CssSliceFromText("rgb"),
                                                   CssRangeSpan(0u, 10u), NULL),
                      RoboOk);
    RoboCheckEqInt(function.kind, RoboCssValueKindFunction);
    RoboCheck(CssSliceEqualsAscii(CssValueName(&function), "rgb"));
    RoboCheckEqInt(CssValueDepth(&function), 1u);

    /* A function needs a name; an empty one is refused. */
    RoboCheckEqInt(CssValueMakeFunction(&function, CssSliceEmpty(),
                                                   CssRangeAt(0u), NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(function.kind, RoboCssValueKindFunction);

    /* A block records the bracket it opened with *and* the one that closes it,
     * so a recovery path can tell a matched block from an unterminated one. */
    RoboCheckEqInt(CssValueMakeBlock(&block, RoboCssTokenLeftSquare,
                                                CssRangeSpan(0u, 9u), NULL),
                      RoboOk);
    RoboCheckEqInt(block.kind, RoboCssValueKindBlock);
    RoboCheckEqInt(block.open, RoboCssTokenLeftSquare);
    RoboCheckEqInt(block.close, RoboCssTokenRightSquare);
    RoboCheckEqInt(CssValueMakeBlock(&block, RoboCssTokenSemicolon,
                                                CssRangeAt(0u), NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(block.close, RoboCssTokenRightSquare);

    CssValueDispose(&function);
    CssValueDispose(&block);
    CssSourceDispose(&source);
}

static void TestCssValueNesting(void)
{
    CssSource source = RoboCssCleared;
    CssValue list = RoboCssCleared;
    CssValue function = RoboCssCleared;
    CssValue block = RoboCssCleared;

    CssFixture();
    CssSourceOfString(&source, "translate(1px,2px)");

    /* list > function > (block > token, token): the shape a real declaration
     * value has, built the way the parser will build it — innermost first. */
    CssValueInit(&block);
    RoboCheckEqInt(CssValueMakeBlock(&block, RoboCssTokenLeftParen,
                                                CssRangeSpan(9u, 5u), NULL),
                      RoboOk);
    {
        CssValue inner = RoboCssCleared;

        CssValueTokenAt(&inner, &source, "1px", NULL);
        RoboCheckEqInt(CssValueAppend(&block, &inner, NULL), RoboOk);
        RoboCheck(CssValueIsEmpty(&inner));
        CssValueTokenAt(&inner, &source, "2px", NULL);
        RoboCheckEqInt(CssValueAppend(&block, &inner, NULL), RoboOk);
    }

    CssValueInit(&function);
    RoboCheckEqInt(CssValueMakeFunction(&function,
                                                   CssSliceFromText("translate"),
                                                   CssRangeSpan(0u, 14u), NULL),
                      RoboOk);
    RoboCheckEqInt(CssValueAppend(&function, &block, NULL), RoboOk);

    CssValueInit(&list);
    RoboCheckEqInt(CssValueMakeList(&list, NULL), RoboOk);
    RoboCheckEqInt(CssValueAppend(&list, &function, NULL), RoboOk);

    RoboCheckEqInt(CssValueChildCount(&list), 1u);
    RoboCheckEqInt(CssValueChildCount(CssValueChild(&list, 0u)), 1u);
    RoboCheckEqInt(
        CssValueChildCount(CssValueChild(CssValueChild(&list, 0u), 0u)),
        2u);
    RoboCheckEqInt(CssValueDepth(&list), 4u);
    /* list + function + block + the two tokens = 5 records. */
    RoboCheckEqInt(CssValueTreeCount(&list), 5u);
    RoboCheck(CssValueChild(&list, 1u) == NULL);
    RoboCheck(CssValueChild(NULL, 0u) == NULL);

    CssValueDispose(&list);
    RoboCheck(CssValueIsEmpty(&list));
    RoboCheckEqInt(CssValueDepth(&list), 0u);
    CssSourceDispose(&source);
}

static void TestCssValueAppendMoves(void)
{
    CssValue list = RoboCssCleared;
    CssValue child = RoboCssCleared;

    CssFixture();
    CssValueInit(&list);
    RoboCheckEqInt(CssValueMakeList(&list, NULL), RoboOk);

    /* A child is moved in, not shared: after the append the caller's value is
     * NONE, so there is exactly one owner and no double free is possible. */
    RoboCheckEqInt(CssValueMakeList(&child, NULL), RoboOk);
    {
        CssValue grandchild = RoboCssCleared;

        RoboCheckEqInt(CssValueMakeList(&grandchild, NULL), RoboOk);
        RoboCheckEqInt(CssValueAppend(&child, &grandchild, NULL), RoboOk);
        RoboCheck(CssValueIsEmpty(&grandchild));
        RoboCheckEqInt(CssValueChildCount(&child), 1u);
    }
    RoboCheckEqInt(CssValueAppend(&list, &child, NULL), RoboOk);
    RoboCheck(CssValueIsEmpty(&child));
    RoboCheckEqInt(CssValueChildCount(&child), 0u);
    RoboCheckEqInt(CssValueChildCount(&list), 1u);
    RoboCheckEqInt(CssValueChildCount(CssValueChild(&list, 0u)), 1u);

    /* A refused append leaves the child untouched: it still owns its tree. */
    {
        CssValue token = RoboCssCleared;
        CssValue sourceValue = RoboCssCleared;

        CssValueInit(&token);
        CssValueInit(&sourceValue);
        RoboCheckEqInt(CssValueMakeList(&sourceValue, NULL), RoboOk);
        RoboCheckEqInt(CssValueAppend(&token, &sourceValue, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(sourceValue.kind, RoboCssValueKindList);
        CssValueDispose(&sourceValue);
    }

    CssValueDispose(&list);
}

static void TestCssValueRejectsBadArguments(void)
{
    CssValue value = RoboCssCleared;
    CssValue other = RoboCssCleared;
    CssDiag diag;

    CssFixture();
    CssValueInit(&value);
    CssValueInit(&other);

    CssDiagReset(&diag);
    RoboCheckEqInt(CssValueAppend(NULL, &other, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(diag.domain, RoboCssDomainValue);
    RoboCheckEqInt(CssValueAppend(&value, NULL, NULL),
                      RoboInvalidArgument);
    /* A value cannot be its own child: that is how a cycle would start. */
    RoboCheckEqInt(CssValueAppend(&value, &value, NULL),
                      RoboInvalidArgument);

    /* A TOKEN value holds no children, so appending into it is refused rather
     * than silently turning the leaf into a container. */
    RoboCheckEqInt(CssValueMakeList(&other, NULL), RoboOk);
    RoboCheckEqInt(CssValueAppend(&value, &other, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(other.kind, RoboCssValueKindList);

    RoboCheckEqInt(CssValueMakeList(NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(CssValueMove(NULL, &other, NULL), RoboInvalidArgument);
    RoboCheckEqInt(CssValueMove(&other, NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(CssValueMove(&other, &other, NULL),
                      RoboInvalidArgument);

    RoboCheck(CssValueIsEmpty(NULL));
    RoboCheckEqInt(CssValueChildCount(NULL), 0u);
    RoboCheckEqInt(CssValueDepth(NULL), 0u);
    RoboCheckEqInt(CssValueTreeCount(NULL), 0u);
    RoboCheck(CssSliceIsEmpty(CssValueTokenText(NULL)));
    RoboCheck(CssSliceIsEmpty(CssValueName(NULL)));
    RoboCheckEqInt(CssValueRange(NULL).begin, 0u);
    RoboCheckEqInt(CssValueToken(NULL).type, RoboCssTokenNone);
    CssValueDispose(NULL);
    CssValueClearChildren(NULL);

    CssValueDispose(&other);
    CssValueDispose(&value);
}

static void TestCssValueMoveTransfersOwnership(void)
{
    CssValue from = RoboCssCleared;
    CssValue to = RoboCssCleared;
    CssValue token = RoboCssCleared;
    CssSource source = RoboCssCleared;

    CssFixture();
    CssSourceOfString(&source, "owned");

    RoboCheckEqInt(CssValueMakeList(&from, NULL), RoboOk);
    CssValueTokenAt(&token, &source, "owned", NULL);
    RoboCheckEqInt(CssValueAppend(&from, &token, NULL), RoboOk);

    /* Moving into a value that already holds a tree disposes that tree: the
     * destination never accumulates two owners. */
    RoboCheckEqInt(CssValueMakeList(&to, NULL), RoboOk);
    RoboCheckEqInt(CssValueMove(&from, &to, NULL), RoboOk);
    RoboCheck(CssValueIsEmpty(&from));
    RoboCheckEqInt(to.kind, RoboCssValueKindList);
    RoboCheckEqInt(CssValueChildCount(&to), 1u);
    /* The moved child is the token that was appended, byte for byte. */
    RoboCheck(CssSliceEqualsAscii(
        CssValueTokenText(CssValueChild(&to, 0u)), "owned"));

    /* Moving an empty value empties the destination, which is the honest
     * meaning of "move this nothing here". */
    RoboCheckEqInt(CssValueMove(&from, &to, NULL), RoboOk);
    RoboCheck(CssValueIsEmpty(&to));

    CssValueDispose(&to);
    CssValueDispose(&from);
    CssSourceDispose(&source);
}

static void TestCssValueLifetimeBalances(void)
{
    CssSource source = RoboCssCleared;
    int weEnabled;
    size_t baseline;

    CssFixture();
    CssSourceOfString(&source, "4px");
    weEnabled = CssAllocatorEnsureEnabled();
    baseline = MemDebugLiveCount();
    {
        int i;

        for (i = 0; i < 32; i++) {
            CssValue list = RoboCssCleared;
            CssValue function = RoboCssCleared;
            CssValue block = RoboCssCleared;
            CssValue token = RoboCssCleared;
            int j;

            RoboCheckEqInt(CssValueMakeList(&list, NULL), RoboOk);
            RoboCheckEqInt(CssValueMakeFunction(&function,
                                                           CssSliceFromText("f"),
                                                           CssRangeAt(0u), NULL),
                              RoboOk);
            RoboCheckEqInt(CssValueMakeBlock(&block,
                                                        RoboCssTokenLeftCurly,
                                                        CssRangeAt(0u), NULL),
                              RoboOk);
            for (j = 0; j < 4; j++) {
                CssValueTokenAt(&token, &source, "4px", NULL);
                RoboCheckEqInt(CssValueAppend(&block, &token, NULL),
                                  RoboOk);
            }
            RoboCheckEqInt(CssValueAppend(&function, &block, NULL), RoboOk);
            RoboCheckEqInt(CssValueAppend(&list, &function, NULL), RoboOk);
            /* list + function + block + four tokens = 7 records; four levels. */
            RoboCheckEqInt(CssValueTreeCount(&list), 7u);
            RoboCheckEqInt(CssValueDepth(&list), 4u);

            /* clear_children frees the tree but keeps the value usable. */
            CssValueClearChildren(&list);
            RoboCheckEqInt(CssValueChildCount(&list), 0u);
            RoboCheckEqInt(list.kind, RoboCssValueKindList);
            CssValueDispose(&list);
            RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);
        }
    }
    RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);
    CssAllocatorRestore(weEnabled);
    CssSourceDispose(&source);
}

void TestRegisterCssValue(void);

void TestRegisterCssValue(void)
{
    TestRegister("css.value.kinds", TestCssValueKinds);
    TestRegister("css.value.token", TestCssValueToken);
    TestRegister("css.value.function_and_block", TestCssValueFunctionAndBlock);
    TestRegister("css.value.nesting", TestCssValueNesting);
    TestRegister("css.value.append_moves", TestCssValueAppendMoves);
    TestRegister("css.value.rejects_bad_arguments",
                       TestCssValueRejectsBadArguments);
    TestRegister("css.value.move_transfers_ownership",
                       TestCssValueMoveTransfersOwnership);
    TestRegister("css.value.lifetime_balances", TestCssValueLifetimeBalances);
}
