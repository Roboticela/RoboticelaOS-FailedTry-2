/*
 * RoboLayout constraints suite (Phase 10.1) — layout.constraints.*
 *
 * What a box is being laid out *into*. Two decisions carry the whole file, and
 * both are asserted from the side that would break if they were wrong:
 *
 *   unset is a flag, not a sentinel. A recorded zero and a field nobody set
 *   produce the same arithmetic and are different answers, so every getter is
 *   asserted for *both* -- the zero that is known and the field that is not. A
 *   header that used a huge number for "unbounded" would pass the first assert
 *   and fail the second, which is exactly the drift this suite exists to catch.
 *
 *   a negative size is refused, never reinterpreted. `available = -1` is not
 *   "unbounded" and not "zero"; it is a caller bug, and the call says so rather
 *   than clamping it silently into range.
 *
 * Nothing here is a sizing rule. `clamp` applies a minimum and a maximum to a
 * size somebody else decided, and holds no opinion about what that size should
 * have been -- no percentage resolution, no `auto` filling, no intrinsic sizing.
 * Those are 10.8, and the tests below are written so that a goal which moved one
 * of them into this record would fail rather than pass.
 */

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Empty state                                                         */
/* ------------------------------------------------------------------ */

static void TestConstraintsEmpty(void)
{
    LayoutConstraints constraints;
    LayoutSize size;

    LayoutFixture();

    /* Nothing is known, which is the honest starting point for a pass. */
    LayoutConstraintsInit(&constraints);
    RoboCheckEqInt(LayoutConstraintsHasAvailable(&constraints), 0);
    RoboCheckEqInt(LayoutConstraintsHasMinimum(&constraints), 0);
    RoboCheckEqInt(LayoutConstraintsHasMaximum(&constraints), 0);
    RoboCheckEqInt(LayoutConstraintsIsValid(&constraints), 1);

    /* An unset field is NOT_FOUND, which is a different answer from a recorded
     * zero -- that difference is the reason these do not simply return a size. */
    RoboCheckEqInt(LayoutConstraintsAvailable(&constraints, &size), RoboNotFound);
    RoboCheckEqInt(LayoutConstraintsMinimum(&constraints, &size), RoboNotFound);
    RoboCheckEqInt(LayoutConstraintsMaximum(&constraints, &size), RoboNotFound);

    /* clear is init by another name, and it discards values rather than only
     * flags. */
    constraints.available.width = 400;
    constraints.available.height = 300;
    constraints.hasAvailable = 1;
    LayoutConstraintsClear(&constraints);
    RoboCheckEqInt(LayoutConstraintsHasAvailable(&constraints), 0);
    RoboCheckEqInt(LayoutConstraintsAvailable(&constraints, &size), RoboNotFound);

    /* Both are safe on NULL: "no record" and "an empty record" are one answer. */
    LayoutConstraintsInit(NULL);
    LayoutConstraintsClear(NULL);
    RoboCheckEqInt(LayoutConstraintsHasAvailable(NULL), 0);
    RoboCheckEqInt(LayoutConstraintsIsValid(NULL), 0);

    /* An argument that is missing is a caller bug, not a miss -- and the size is
     * a defined value here rather than whatever the declaration left, because a
     * test that passed uninitialised memory would be asserting about a stack slot. */
    size.width = 800;
    size.height = 600;
    RoboCheckEqInt(LayoutConstraintsAvailable(NULL, &size), RoboInvalidArgument);
    RoboCheckEqInt(LayoutConstraintsMinimum(NULL, &size), RoboInvalidArgument);
    RoboCheckEqInt(LayoutConstraintsMaximum(NULL, &size), RoboInvalidArgument);
    RoboCheckEqInt(LayoutConstraintsAvailable(&constraints, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutConstraintsSetAvailable(NULL, size, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutConstraintsSetMinimum(NULL, size, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutConstraintsSetMaximum(NULL, size, NULL),
                      RoboInvalidArgument);

    /* And with every argument present, the call works on the record the earlier
     * clears left empty. */
    RoboCheckEqInt(LayoutConstraintsSetAvailable(&constraints, size, NULL), RoboOk);
    RoboCheckEqInt(LayoutConstraintsHasAvailable(&constraints), 1);
    RoboCheckEqInt(LayoutConstraintsAvailable(&constraints, &size), RoboOk);
    RoboCheckEqInt(size.width, 800);
}

/* ------------------------------------------------------------------ */
/* Recording and reading back                                          */
/* ------------------------------------------------------------------ */

static void TestConstraintsSetGet(void)
{
    LayoutConstraints constraints;
    LayoutSize out;
    LayoutDiag diag = RoboLayoutCleared;
    LayoutSize available;
    LayoutSize minimum;
    LayoutSize maximum;

    LayoutFixture();
    LayoutConstraintsInit(&constraints);

    /*
     * Three *different* sizes, so a getter that read the wrong field would fail
     * here rather than agree by coincidence.
     */
    available.width = 800;
    available.height = 600;
    minimum.width = 10;
    minimum.height = 20;
    maximum.width = 700;
    maximum.height = 500;

    RoboCheckEqInt(LayoutConstraintsSetAvailable(&constraints, available, &diag),
                      RoboOk);
    RoboCheckEqInt(LayoutConstraintsSetMinimum(&constraints, minimum, &diag),
                      RoboOk);
    RoboCheckEqInt(LayoutConstraintsSetMaximum(&constraints, maximum, &diag),
                      RoboOk);
    /* A successful call leaves no failure behind. */
    RoboCheckEqInt(diag.code, RoboOk);

    RoboCheckEqInt(LayoutConstraintsHasAvailable(&constraints), 1);
    RoboCheckEqInt(LayoutConstraintsHasMinimum(&constraints), 1);
    RoboCheckEqInt(LayoutConstraintsHasMaximum(&constraints), 1);

    RoboCheckEqInt(LayoutConstraintsAvailable(&constraints, &out), RoboOk);
    RoboCheckEqInt(out.width, 800);
    RoboCheckEqInt(out.height, 600);
    RoboCheckEqInt(LayoutConstraintsMinimum(&constraints, &out), RoboOk);
    RoboCheckEqInt(out.width, 10);
    RoboCheckEqInt(out.height, 20);
    RoboCheckEqInt(LayoutConstraintsMaximum(&constraints, &out), RoboOk);
    RoboCheckEqInt(out.width, 700);
    RoboCheckEqInt(out.height, 500);
}

static void TestConstraintsZeroIsKnown(void)
{
    LayoutConstraints constraints;
    LayoutSize zero;
    LayoutSize out;

    LayoutFixture();
    LayoutConstraintsInit(&constraints);
    zero.width = 0;
    zero.height = 0;

    /* A zero size is a *known* zero: an empty containing block is a legitimate
     * thing to be laid out into, and it is not the same fact as "nobody said". */
    RoboCheckEqInt(LayoutConstraintsSetAvailable(&constraints, zero, NULL), RoboOk);
    RoboCheckEqInt(LayoutConstraintsHasAvailable(&constraints), 1);
    RoboCheckEqInt(LayoutConstraintsAvailable(&constraints, &out), RoboOk);
    RoboCheckEqInt(out.width, 0);
    RoboCheckEqInt(out.height, 0);

    /* And with no minimum recorded, a zero desired size passes through as a
     * zero rather than being raised to something invented. */
    RoboCheckEqInt(LayoutConstraintsClamp(&constraints, zero, &out, NULL), RoboOk);
    RoboCheckEqInt(out.width, 0);
    RoboCheckEqInt(out.height, 0);
}

static void TestConstraintsNegativeRefused(void)
{
    LayoutConstraints constraints;
    LayoutSize out;
    LayoutSize good;
    LayoutSize bad;
    LayoutDiag diag = RoboLayoutCleared;

    LayoutFixture();
    LayoutConstraintsInit(&constraints);

    good.width = 640;
    good.height = 480;
    RoboCheckEqInt(LayoutConstraintsSetAvailable(&constraints, good, NULL), RoboOk);

    bad.width = -1;
    bad.height = 480;
    RoboCheckEqInt(LayoutConstraintsSetAvailable(&constraints, bad, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(diag.code, RoboInvalidArgument);
    RoboCheckEqInt((int)diag.domain, (int)RoboLayoutDomainConstraints);
    /* Refused, not clamped: the record still holds what it held. */
    RoboCheckEqInt(LayoutConstraintsAvailable(&constraints, &out), RoboOk);
    RoboCheckEqInt(out.width, 640);

    bad.width = 640;
    bad.height = -1;
    RoboCheckEqInt(LayoutConstraintsSetMinimum(&constraints, bad, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutConstraintsHasMinimum(&constraints), 0);

    bad.width = -640;
    bad.height = -480;
    RoboCheckEqInt(LayoutConstraintsSetMaximum(&constraints, bad, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutConstraintsHasMaximum(&constraints), 0);

    /* The same rule on the way back in through clamp. */
    RoboCheckEqInt(LayoutConstraintsClamp(&constraints, bad, &out, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(diag.code, RoboInvalidArgument);
    RoboCheckEqInt(LayoutConstraintsClamp(NULL, good, &out, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutConstraintsClamp(&constraints, good, NULL, NULL),
                      RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* Contradiction                                                       */
/* ------------------------------------------------------------------ */

static void TestConstraintsValidity(void)
{
    LayoutConstraints constraints;
    LayoutSize smaller;
    LayoutSize bigger;

    LayoutFixture();
    LayoutConstraintsInit(&constraints);

    /* One bound is consistent with itself, and "nothing is known" is legal. */
    RoboCheckEqInt(LayoutConstraintsIsValid(&constraints), 1);

    smaller.width = 100;
    smaller.height = 100;
    bigger.width = 200;
    bigger.height = 200;
    RoboCheckEqInt(LayoutConstraintsSetMinimum(&constraints, smaller, NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutConstraintsIsValid(&constraints), 1);
    RoboCheckEqInt(LayoutConstraintsSetMaximum(&constraints, bigger, NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutConstraintsIsValid(&constraints), 1);

    /* Equal bounds are not a contradiction. */
    RoboCheckEqInt(LayoutConstraintsSetMaximum(&constraints, smaller, NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutConstraintsIsValid(&constraints), 1);

    /* A minimum above the maximum is. It is not repaired at set time, because
     * the two may legitimately be set in either order and the combination only
     * becomes a problem once both exist. */
    RoboCheckEqInt(LayoutConstraintsSetMinimum(&constraints, bigger, NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutConstraintsIsValid(&constraints), 0);

    /* One axis contradicting is enough -- the other being fine does not rescue
     * it. */
    bigger.height = 50;
    RoboCheckEqInt(LayoutConstraintsSetMaximum(&constraints, bigger, NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutConstraintsIsValid(&constraints), 0);
}

/* ------------------------------------------------------------------ */
/* Clamping                                                            */
/* ------------------------------------------------------------------ */

static void TestConstraintsClampBounds(void)
{
    LayoutConstraints constraints;
    LayoutSize desired;
    LayoutSize out;
    LayoutSize hundred;

    hundred.width = 100;
    hundred.height = 100;
    LayoutFixture();
    LayoutConstraintsInit(&constraints);

    desired.width = 300;
    desired.height = 300;

    /* No bounds: the desired size passes through unchanged. Clamp has no opinion
     * about what the size should have been. */
    out.width = -1;
    out.height = -1;
    RoboCheckEqInt(LayoutConstraintsClamp(&constraints, desired, &out, NULL), RoboOk);
    RoboCheckEqInt(out.width, 300);
    RoboCheckEqInt(out.height, 300);

    /* Maximum only: it lowers a too-large size and leaves a small one alone. */
    constraints.maximum.width = 200;
    constraints.maximum.height = 200;
    RoboCheckEqInt(LayoutConstraintsSetMaximum(&constraints, constraints.maximum,
                                                          NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutConstraintsClamp(&constraints, desired, &out, NULL), RoboOk);
    RoboCheckEqInt(out.width, 200);
    RoboCheckEqInt(out.height, 200);

    desired.width = 50;
    desired.height = 50;
    RoboCheckEqInt(LayoutConstraintsClamp(&constraints, desired, &out, NULL), RoboOk);
    RoboCheckEqInt(out.width, 50);
    RoboCheckEqInt(out.height, 50);

    /* Minimum only: it raises a too-small size and leaves a large one alone. */
    LayoutConstraintsClear(&constraints);
    RoboCheckEqInt(LayoutConstraintsSetMinimum(&constraints, hundred, NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutConstraintsClamp(&constraints, desired, &out, NULL), RoboOk);
    RoboCheckEqInt(out.width, 100);
    RoboCheckEqInt(out.height, 100);

    desired.width = 400;
    desired.height = 400;
    RoboCheckEqInt(LayoutConstraintsClamp(&constraints, desired, &out, NULL), RoboOk);
    RoboCheckEqInt(out.width, 400);
    RoboCheckEqInt(out.height, 400);

    /* Both: the minimum is applied second, so the minimum wins a tie -- CSS's own
     * resolution, and the one that keeps a box visible. */
    constraints.minimum.width = 150;
    constraints.minimum.height = 150;
    constraints.maximum.width = 150;
    constraints.maximum.height = 150;
    RoboCheckEqInt(LayoutConstraintsSetMinimum(&constraints, constraints.minimum,
                                                          NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutConstraintsSetMaximum(&constraints, constraints.maximum,
                                                          NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutConstraintsClamp(&constraints, desired, &out, NULL), RoboOk);
    RoboCheckEqInt(out.width, 150);
    RoboCheckEqInt(out.height, 150);

    /* `available` is deliberately NOT an implicit maximum: whether a box fills
     * the space it is given is a formatting-context decision, and a constraint
     * record that answered it would be answering for Phase 10.3. */
    LayoutConstraintsClear(&constraints);
    constraints.available.width = 80;
    constraints.available.height = 80;
    RoboCheckEqInt(LayoutConstraintsSetAvailable(&constraints, constraints.available,
                                                            NULL),
                      RoboOk);
    desired.width = 5000;
    desired.height = 5000;
    RoboCheckEqInt(LayoutConstraintsClamp(&constraints, desired, &out, NULL), RoboOk);
    RoboCheckEqInt(out.width, 5000);
    RoboCheckEqInt(out.height, 5000);
}

static void TestConstraintsClampContradiction(void)
{
    LayoutConstraints constraints;
    LayoutSize desired;
    LayoutSize out;
    LayoutDiag diag = RoboLayoutCleared;

    LayoutFixture();
    LayoutConstraintsInit(&constraints);

    constraints.minimum.width = 500;
    constraints.minimum.height = 500;
    constraints.maximum.width = 100;
    constraints.maximum.height = 100;
    RoboCheckEqInt(LayoutConstraintsSetMinimum(&constraints, constraints.minimum,
                                                          NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutConstraintsSetMaximum(&constraints, constraints.maximum,
                                                          NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutConstraintsIsValid(&constraints), 0);

    /* A contradiction has no answer, and picking one would hide a bug the caller
     * can fix. The output is untouched, so a caller that ignored the code cannot
     * read a size nobody can defend as if it were a result. */
    desired.width = 300;
    desired.height = 300;
    out.width = -7;
    out.height = -7;
    RoboCheckEqInt(LayoutConstraintsClamp(&constraints, desired, &out, &diag),
                      RoboNotFound);
    RoboCheckEqInt(diag.code, RoboNotFound);
    RoboCheckEqInt((int)diag.domain, (int)RoboLayoutDomainConstraints);
    RoboCheckEqInt(out.width, -7);
    RoboCheckEqInt(out.height, -7);
}

/* ------------------------------------------------------------------ */
/* Equality                                                            */
/* ------------------------------------------------------------------ */

static void TestConstraintsEquality(void)
{
    LayoutConstraints a;
    LayoutConstraints b;
    LayoutSize size;

    LayoutFixture();
    LayoutConstraintsInit(&a);
    LayoutConstraintsInit(&b);

    RoboCheckEqInt(LayoutConstraintsEqual(&a, &b), 1);
    RoboCheckEqInt(LayoutConstraintsEqual(NULL, NULL), 1);
    RoboCheckEqInt(LayoutConstraintsEqual(&a, NULL), 0);
    RoboCheckEqInt(LayoutConstraintsEqual(NULL, &b), 0);

    size.width = 640;
    size.height = 480;
    RoboCheckEqInt(LayoutConstraintsSetAvailable(&a, size, NULL), RoboOk);
    /* One recorded and one unset are not equal, even though the unset record's
     * bytes happen to be zero. */
    RoboCheckEqInt(LayoutConstraintsEqual(&a, &b), 0);

    RoboCheckEqInt(LayoutConstraintsSetAvailable(&b, size, NULL), RoboOk);
    RoboCheckEqInt(LayoutConstraintsEqual(&a, &b), 1);

    /* A different value in a set field breaks it... */
    b.available.width = 641;
    RoboCheckEqInt(LayoutConstraintsEqual(&a, &b), 0);

    /* ...and an *unset* field's bytes are not compared, because they are
     * whatever the caller's stack held and comparing them would make two equal
     * records unequal. */
    LayoutConstraintsInit(&b);
    b.hasAvailable = 1;
    RoboCheckEqInt(LayoutConstraintsSetAvailable(&b, size, NULL), RoboOk);
    b.minimum.width = 12345;
    b.minimum.height = 12345;
    b.maximum.width = -99;
    b.maximum.height = -99;
    RoboCheckEqInt(LayoutConstraintsEqual(&a, &b), 1);
}

static void TestConstraintsAllocatorBalance(void)
{
    size_t before;
    LayoutConstraints constraints;
    LayoutSize size;
    LayoutSize out;
    int i;

    LayoutFixture();
    before = LayoutLiveBlocks();
    LayoutConstraintsInit(&constraints);

    /* A constraint record is a by-value struct that owns no allocation, so a
     * caller can put it on the stack and copy it. Asserted rather than assumed,
     * because the day one of these calls starts allocating is the day a pass
     * holds a pointer into a temporary. */
    for (i = 0; i < 64; i++) {
        size.width = i;
        size.height = i * 2;
        RoboCheckEqInt(LayoutConstraintsSetAvailable(&constraints, size, NULL),
                          RoboOk);
        RoboCheckEqInt(LayoutConstraintsSetMinimum(&constraints, size, NULL),
                          RoboOk);
        RoboCheckEqInt(LayoutConstraintsSetMaximum(&constraints, size, NULL),
                          RoboOk);
        RoboCheckEqInt(LayoutConstraintsClamp(&constraints, size, &out, NULL),
                          RoboOk);
        (void)LayoutConstraintsEqual(&constraints, &constraints);
        RoboCheckEqInt(LayoutConstraintsIsValid(&constraints), 1);
    }

    RoboCheckEqInt(LayoutLiveBlocks(), before);
}

/* ------------------------------------------------------------------ */

void TestRegisterLayoutConstraints(void);

void TestRegisterLayoutConstraints(void)
{
    TestRegister("layout.constraints.empty", TestConstraintsEmpty);
    TestRegister("layout.constraints.set_get", TestConstraintsSetGet);
    TestRegister("layout.constraints.zero_is_known", TestConstraintsZeroIsKnown);
    TestRegister("layout.constraints.negative_refused", TestConstraintsNegativeRefused);
    TestRegister("layout.constraints.validity", TestConstraintsValidity);
    TestRegister("layout.constraints.clamp_bounds", TestConstraintsClampBounds);
    TestRegister("layout.constraints.clamp_contradiction",
                       TestConstraintsClampContradiction);
    TestRegister("layout.constraints.equality", TestConstraintsEquality);
    TestRegister("layout.constraints.allocator_balance",
                       TestConstraintsAllocatorBalance);
}
