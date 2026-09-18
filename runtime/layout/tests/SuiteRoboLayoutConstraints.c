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

static void test_constraints_empty(void)
{
    robo_layout_constraints_t constraints;
    robo_layout_size_t size;

    layout_fixture();

    /* Nothing is known, which is the honest starting point for a pass. */
    robo_layout_constraints_init(&constraints);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_has_available(&constraints), 0);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_has_minimum(&constraints), 0);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_has_maximum(&constraints), 0);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_is_valid(&constraints), 1);

    /* An unset field is NOT_FOUND, which is a different answer from a recorded
     * zero -- that difference is the reason these do not simply return a size. */
    ROBO_CHECK_EQ_INT(robo_layout_constraints_available(&constraints, &size), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_minimum(&constraints, &size), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_maximum(&constraints, &size), ROBO_NOT_FOUND);

    /* clear is init by another name, and it discards values rather than only
     * flags. */
    constraints.available.width = 400;
    constraints.available.height = 300;
    constraints.has_available = 1;
    robo_layout_constraints_clear(&constraints);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_has_available(&constraints), 0);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_available(&constraints, &size), ROBO_NOT_FOUND);

    /* Both are safe on NULL: "no record" and "an empty record" are one answer. */
    robo_layout_constraints_init(NULL);
    robo_layout_constraints_clear(NULL);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_has_available(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_is_valid(NULL), 0);

    /* An argument that is missing is a caller bug, not a miss -- and the size is
     * a defined value here rather than whatever the declaration left, because a
     * test that passed uninitialised memory would be asserting about a stack slot. */
    size.width = 800;
    size.height = 600;
    ROBO_CHECK_EQ_INT(robo_layout_constraints_available(NULL, &size), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_minimum(NULL, &size), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_maximum(NULL, &size), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_available(&constraints, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_set_available(NULL, size, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_set_minimum(NULL, size, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_set_maximum(NULL, size, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* And with every argument present, the call works on the record the earlier
     * clears left empty. */
    ROBO_CHECK_EQ_INT(robo_layout_constraints_set_available(&constraints, size, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_has_available(&constraints), 1);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_available(&constraints, &size), ROBO_OK);
    ROBO_CHECK_EQ_INT(size.width, 800);
}

/* ------------------------------------------------------------------ */
/* Recording and reading back                                          */
/* ------------------------------------------------------------------ */

static void test_constraints_set_get(void)
{
    robo_layout_constraints_t constraints;
    robo_layout_size_t out;
    robo_layout_diag_t diag = ROBO_LAYOUT_CLEARED;
    robo_layout_size_t available;
    robo_layout_size_t minimum;
    robo_layout_size_t maximum;

    layout_fixture();
    robo_layout_constraints_init(&constraints);

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

    ROBO_CHECK_EQ_INT(robo_layout_constraints_set_available(&constraints, available, &diag),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_set_minimum(&constraints, minimum, &diag),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_set_maximum(&constraints, maximum, &diag),
                      ROBO_OK);
    /* A successful call leaves no failure behind. */
    ROBO_CHECK_EQ_INT(diag.code, ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_layout_constraints_has_available(&constraints), 1);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_has_minimum(&constraints), 1);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_has_maximum(&constraints), 1);

    ROBO_CHECK_EQ_INT(robo_layout_constraints_available(&constraints, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT(out.width, 800);
    ROBO_CHECK_EQ_INT(out.height, 600);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_minimum(&constraints, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT(out.width, 10);
    ROBO_CHECK_EQ_INT(out.height, 20);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_maximum(&constraints, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT(out.width, 700);
    ROBO_CHECK_EQ_INT(out.height, 500);
}

static void test_constraints_zero_is_known(void)
{
    robo_layout_constraints_t constraints;
    robo_layout_size_t zero;
    robo_layout_size_t out;

    layout_fixture();
    robo_layout_constraints_init(&constraints);
    zero.width = 0;
    zero.height = 0;

    /* A zero size is a *known* zero: an empty containing block is a legitimate
     * thing to be laid out into, and it is not the same fact as "nobody said". */
    ROBO_CHECK_EQ_INT(robo_layout_constraints_set_available(&constraints, zero, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_has_available(&constraints), 1);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_available(&constraints, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT(out.width, 0);
    ROBO_CHECK_EQ_INT(out.height, 0);

    /* And with no minimum recorded, a zero desired size passes through as a
     * zero rather than being raised to something invented. */
    ROBO_CHECK_EQ_INT(robo_layout_constraints_clamp(&constraints, zero, &out, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(out.width, 0);
    ROBO_CHECK_EQ_INT(out.height, 0);
}

static void test_constraints_negative_refused(void)
{
    robo_layout_constraints_t constraints;
    robo_layout_size_t out;
    robo_layout_size_t good;
    robo_layout_size_t bad;
    robo_layout_diag_t diag = ROBO_LAYOUT_CLEARED;

    layout_fixture();
    robo_layout_constraints_init(&constraints);

    good.width = 640;
    good.height = 480;
    ROBO_CHECK_EQ_INT(robo_layout_constraints_set_available(&constraints, good, NULL), ROBO_OK);

    bad.width = -1;
    bad.height = 480;
    ROBO_CHECK_EQ_INT(robo_layout_constraints_set_available(&constraints, bad, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(diag.code, ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_LAYOUT_DOMAIN_CONSTRAINTS);
    /* Refused, not clamped: the record still holds what it held. */
    ROBO_CHECK_EQ_INT(robo_layout_constraints_available(&constraints, &out), ROBO_OK);
    ROBO_CHECK_EQ_INT(out.width, 640);

    bad.width = 640;
    bad.height = -1;
    ROBO_CHECK_EQ_INT(robo_layout_constraints_set_minimum(&constraints, bad, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_has_minimum(&constraints), 0);

    bad.width = -640;
    bad.height = -480;
    ROBO_CHECK_EQ_INT(robo_layout_constraints_set_maximum(&constraints, bad, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_has_maximum(&constraints), 0);

    /* The same rule on the way back in through clamp. */
    ROBO_CHECK_EQ_INT(robo_layout_constraints_clamp(&constraints, bad, &out, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(diag.code, ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_clamp(NULL, good, &out, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_clamp(&constraints, good, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
}

/* ------------------------------------------------------------------ */
/* Contradiction                                                       */
/* ------------------------------------------------------------------ */

static void test_constraints_validity(void)
{
    robo_layout_constraints_t constraints;
    robo_layout_size_t smaller;
    robo_layout_size_t bigger;

    layout_fixture();
    robo_layout_constraints_init(&constraints);

    /* One bound is consistent with itself, and "nothing is known" is legal. */
    ROBO_CHECK_EQ_INT(robo_layout_constraints_is_valid(&constraints), 1);

    smaller.width = 100;
    smaller.height = 100;
    bigger.width = 200;
    bigger.height = 200;
    ROBO_CHECK_EQ_INT(robo_layout_constraints_set_minimum(&constraints, smaller, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_is_valid(&constraints), 1);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_set_maximum(&constraints, bigger, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_is_valid(&constraints), 1);

    /* Equal bounds are not a contradiction. */
    ROBO_CHECK_EQ_INT(robo_layout_constraints_set_maximum(&constraints, smaller, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_is_valid(&constraints), 1);

    /* A minimum above the maximum is. It is not repaired at set time, because
     * the two may legitimately be set in either order and the combination only
     * becomes a problem once both exist. */
    ROBO_CHECK_EQ_INT(robo_layout_constraints_set_minimum(&constraints, bigger, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_is_valid(&constraints), 0);

    /* One axis contradicting is enough -- the other being fine does not rescue
     * it. */
    bigger.height = 50;
    ROBO_CHECK_EQ_INT(robo_layout_constraints_set_maximum(&constraints, bigger, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_is_valid(&constraints), 0);
}

/* ------------------------------------------------------------------ */
/* Clamping                                                            */
/* ------------------------------------------------------------------ */

static void test_constraints_clamp_bounds(void)
{
    robo_layout_constraints_t constraints;
    robo_layout_size_t desired;
    robo_layout_size_t out;
    robo_layout_size_t hundred;

    hundred.width = 100;
    hundred.height = 100;
    layout_fixture();
    robo_layout_constraints_init(&constraints);

    desired.width = 300;
    desired.height = 300;

    /* No bounds: the desired size passes through unchanged. Clamp has no opinion
     * about what the size should have been. */
    out.width = -1;
    out.height = -1;
    ROBO_CHECK_EQ_INT(robo_layout_constraints_clamp(&constraints, desired, &out, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(out.width, 300);
    ROBO_CHECK_EQ_INT(out.height, 300);

    /* Maximum only: it lowers a too-large size and leaves a small one alone. */
    constraints.maximum.width = 200;
    constraints.maximum.height = 200;
    ROBO_CHECK_EQ_INT(robo_layout_constraints_set_maximum(&constraints, constraints.maximum,
                                                          NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_clamp(&constraints, desired, &out, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(out.width, 200);
    ROBO_CHECK_EQ_INT(out.height, 200);

    desired.width = 50;
    desired.height = 50;
    ROBO_CHECK_EQ_INT(robo_layout_constraints_clamp(&constraints, desired, &out, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(out.width, 50);
    ROBO_CHECK_EQ_INT(out.height, 50);

    /* Minimum only: it raises a too-small size and leaves a large one alone. */
    robo_layout_constraints_clear(&constraints);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_set_minimum(&constraints, hundred, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_clamp(&constraints, desired, &out, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(out.width, 100);
    ROBO_CHECK_EQ_INT(out.height, 100);

    desired.width = 400;
    desired.height = 400;
    ROBO_CHECK_EQ_INT(robo_layout_constraints_clamp(&constraints, desired, &out, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(out.width, 400);
    ROBO_CHECK_EQ_INT(out.height, 400);

    /* Both: the minimum is applied second, so the minimum wins a tie -- CSS's own
     * resolution, and the one that keeps a box visible. */
    constraints.minimum.width = 150;
    constraints.minimum.height = 150;
    constraints.maximum.width = 150;
    constraints.maximum.height = 150;
    ROBO_CHECK_EQ_INT(robo_layout_constraints_set_minimum(&constraints, constraints.minimum,
                                                          NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_set_maximum(&constraints, constraints.maximum,
                                                          NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_clamp(&constraints, desired, &out, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(out.width, 150);
    ROBO_CHECK_EQ_INT(out.height, 150);

    /* `available` is deliberately NOT an implicit maximum: whether a box fills
     * the space it is given is a formatting-context decision, and a constraint
     * record that answered it would be answering for Phase 10.3. */
    robo_layout_constraints_clear(&constraints);
    constraints.available.width = 80;
    constraints.available.height = 80;
    ROBO_CHECK_EQ_INT(robo_layout_constraints_set_available(&constraints, constraints.available,
                                                            NULL),
                      ROBO_OK);
    desired.width = 5000;
    desired.height = 5000;
    ROBO_CHECK_EQ_INT(robo_layout_constraints_clamp(&constraints, desired, &out, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(out.width, 5000);
    ROBO_CHECK_EQ_INT(out.height, 5000);
}

static void test_constraints_clamp_contradiction(void)
{
    robo_layout_constraints_t constraints;
    robo_layout_size_t desired;
    robo_layout_size_t out;
    robo_layout_diag_t diag = ROBO_LAYOUT_CLEARED;

    layout_fixture();
    robo_layout_constraints_init(&constraints);

    constraints.minimum.width = 500;
    constraints.minimum.height = 500;
    constraints.maximum.width = 100;
    constraints.maximum.height = 100;
    ROBO_CHECK_EQ_INT(robo_layout_constraints_set_minimum(&constraints, constraints.minimum,
                                                          NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_set_maximum(&constraints, constraints.maximum,
                                                          NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_is_valid(&constraints), 0);

    /* A contradiction has no answer, and picking one would hide a bug the caller
     * can fix. The output is untouched, so a caller that ignored the code cannot
     * read a size nobody can defend as if it were a result. */
    desired.width = 300;
    desired.height = 300;
    out.width = -7;
    out.height = -7;
    ROBO_CHECK_EQ_INT(robo_layout_constraints_clamp(&constraints, desired, &out, &diag),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(diag.code, ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_LAYOUT_DOMAIN_CONSTRAINTS);
    ROBO_CHECK_EQ_INT(out.width, -7);
    ROBO_CHECK_EQ_INT(out.height, -7);
}

/* ------------------------------------------------------------------ */
/* Equality                                                            */
/* ------------------------------------------------------------------ */

static void test_constraints_equality(void)
{
    robo_layout_constraints_t a;
    robo_layout_constraints_t b;
    robo_layout_size_t size;

    layout_fixture();
    robo_layout_constraints_init(&a);
    robo_layout_constraints_init(&b);

    ROBO_CHECK_EQ_INT(robo_layout_constraints_equal(&a, &b), 1);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_equal(NULL, NULL), 1);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_equal(&a, NULL), 0);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_equal(NULL, &b), 0);

    size.width = 640;
    size.height = 480;
    ROBO_CHECK_EQ_INT(robo_layout_constraints_set_available(&a, size, NULL), ROBO_OK);
    /* One recorded and one unset are not equal, even though the unset record's
     * bytes happen to be zero. */
    ROBO_CHECK_EQ_INT(robo_layout_constraints_equal(&a, &b), 0);

    ROBO_CHECK_EQ_INT(robo_layout_constraints_set_available(&b, size, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_constraints_equal(&a, &b), 1);

    /* A different value in a set field breaks it... */
    b.available.width = 641;
    ROBO_CHECK_EQ_INT(robo_layout_constraints_equal(&a, &b), 0);

    /* ...and an *unset* field's bytes are not compared, because they are
     * whatever the caller's stack held and comparing them would make two equal
     * records unequal. */
    robo_layout_constraints_init(&b);
    b.has_available = 1;
    ROBO_CHECK_EQ_INT(robo_layout_constraints_set_available(&b, size, NULL), ROBO_OK);
    b.minimum.width = 12345;
    b.minimum.height = 12345;
    b.maximum.width = -99;
    b.maximum.height = -99;
    ROBO_CHECK_EQ_INT(robo_layout_constraints_equal(&a, &b), 1);
}

static void test_constraints_allocator_balance(void)
{
    size_t before;
    robo_layout_constraints_t constraints;
    robo_layout_size_t size;
    robo_layout_size_t out;
    int i;

    layout_fixture();
    before = layout_live_blocks();
    robo_layout_constraints_init(&constraints);

    /* A constraint record is a by-value struct that owns no allocation, so a
     * caller can put it on the stack and copy it. Asserted rather than assumed,
     * because the day one of these calls starts allocating is the day a pass
     * holds a pointer into a temporary. */
    for (i = 0; i < 64; i++) {
        size.width = i;
        size.height = i * 2;
        ROBO_CHECK_EQ_INT(robo_layout_constraints_set_available(&constraints, size, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_constraints_set_minimum(&constraints, size, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_constraints_set_maximum(&constraints, size, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_constraints_clamp(&constraints, size, &out, NULL),
                          ROBO_OK);
        (void)robo_layout_constraints_equal(&constraints, &constraints);
        ROBO_CHECK_EQ_INT(robo_layout_constraints_is_valid(&constraints), 1);
    }

    ROBO_CHECK_EQ_INT(layout_live_blocks(), before);
}

/* ------------------------------------------------------------------ */

void robo_test_register_layout_constraints(void);

void robo_test_register_layout_constraints(void)
{
    robo_test_register("layout.constraints.empty", test_constraints_empty);
    robo_test_register("layout.constraints.set_get", test_constraints_set_get);
    robo_test_register("layout.constraints.zero_is_known", test_constraints_zero_is_known);
    robo_test_register("layout.constraints.negative_refused", test_constraints_negative_refused);
    robo_test_register("layout.constraints.validity", test_constraints_validity);
    robo_test_register("layout.constraints.clamp_bounds", test_constraints_clamp_bounds);
    robo_test_register("layout.constraints.clamp_contradiction",
                       test_constraints_clamp_contradiction);
    robo_test_register("layout.constraints.equality", test_constraints_equality);
    robo_test_register("layout.constraints.allocator_balance",
                       test_constraints_allocator_balance);
}
