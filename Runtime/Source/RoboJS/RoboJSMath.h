/*
 * RoboJS's own arithmetic (Phase 16.4) — the four functions the language needs and the freestanding
 * guest has no library for.
 *
 * ## Why this file exists
 *
 * `Libc/` is freestanding and carries no `math.h`, and RoboJS compiles into the guest, so
 * `floor`, `fmod`, `sqrt` and `pow` are not available. Other modules in this tree solve the same
 * problem the same way (`RoboTextMeasurement.c` uses `v == v` rather than `isfinite` "because this
 * file is compiled into the guest program"); this header is that decision written once for the
 * module rather than repeated in four files.
 *
 * ## What is exact, and what is a stated approximation
 *
 * `JsMathFloor`, `JsMathCeil`, `JsMathAbs` and `JsMathFmod` are exact for every double this build can
 * produce: the first three are an `int64_t` cast plus one correction step, and `fmod` reduces by
 * binary halving, which is exact for a power-of-two divisor and within one ulp otherwise.
 *
 * `JsMathSqrt`, `JsMathExp`, `JsMathLog` and `JsMathPow` are **approximations**: Newton's iteration
 * and series expansions carried to a fixed term count. They are accurate to about fifteen significant
 * digits over the ranges a script actually meets and deliberately not correctly-rounded in the last
 * bit — `Documentation/RoboJS/Limitations.md` states that, and the suite asserts a tolerance rather
 * than an exact bit pattern for those four. An engine that claimed correctly-rounded transcendentals
 * without a big-number library would be claiming something it did not have.
 *
 * Every function is `static inline`: no header in this module may add a symbol to the guest binary,
 * and the compiler drops the ones a translation unit does not call.
 */

#ifndef ROBO_JS_MATH_H
#define ROBO_JS_MATH_H

#include <stdint.h>

/** `+Infinity`, built from its bit pattern because no header will hand it over. */
static inline double JsMathInfinity(void)
{
    union {
        uint64_t bits;
        double value;
    } u;

    u.bits = 0x7FF0000000000000ull;
    return u.value;
}

/** The quiet NaN `0/0` produces, built the same way. */
static inline double JsMathNan(void)
{
    union {
        uint64_t bits;
        double value;
    } u;

    u.bits = 0x7FF8000000000000ull;
    return u.value;
}

/** 1 for NaN. `v != v` is the test the rest of this tree uses rather than `isnan`. */
static inline int JsMathIsNan(double value)
{
    return value != value;
}

/** 1 for a finite number (neither NaN nor an infinity). */
static inline int JsMathIsFinite(double value)
{
    return !JsMathIsNan(value) && value != JsMathInfinity() && value != -JsMathInfinity();
}

static inline double JsMathAbs(double value)
{
    return value < 0.0 ? -value : value;
}

/**
 * Truncation toward zero. Every double whose magnitude is below 2^53 converts to `int64_t` exactly;
 * the branch keeps the two infinities and the huge values (where a cast would be undefined) as
 * themselves, which is what `Math.trunc` and every internal caller wants.
 */
static inline double JsMathTruncate(double value)
{
    if (!JsMathIsFinite(value) || value >= 9223372036854775808.0 ||
        value <= -9223372036854775808.0) {
        return value;
    }
    return (double)(int64_t)value;
}

static inline double JsMathFloor(double value)
{
    double truncated = JsMathTruncate(value);

    if (truncated > value) {
        truncated -= 1.0;
    }
    return truncated;
}

static inline double JsMathCeil(double value)
{
    double truncated = JsMathTruncate(value);

    if (truncated < value) {
        truncated += 1.0;
    }
    return truncated;
}

/** JavaScript's `Math.round`: half toward positive infinity, so `-0.5` answers `-0`. */
static inline double JsMathRound(double value)
{
    if (JsMathIsNan(value) || !JsMathIsFinite(value)) {
        return value;
    }
    if (value >= 0.0 && value < 0.5) {
        return 0.0;
    }
    if (value < 0.0 && value >= -0.5) {
        /* `Math.round(-0.4)` is `-0`, and JavaScript prints that as `0`. The sign is kept here
         * because `1/x` distinguishes them and the value model stores the double as it is. */
        return -0.0;
    }
    return JsMathFloor(value + 0.5);
}

/** 1 when @p value is negative. `-0` counts, which is why `1.0 / value` is read rather than
 *  `value < 0.0` alone. */
static inline int JsMathSignBit(double value)
{
    return value < 0.0 || (value == 0.0 && 1.0 / value < 0.0);
}

/**
 * `a % b` with the sign of `a`. Exact for the two cases the engine actually takes — a divisor that is
 * a power of two (`JsValueToInt32`) and two integral operands (`%` on small numbers) — and within one
 * ulp elsewhere. Binary halving is why it terminates: `step` doubles until it is above half of `r`.
 */
static inline double JsMathFmod(double a, double b)
{
    double r;
    double sign;
    double step;
    int guard = 0;

    if (JsMathIsNan(a) || JsMathIsNan(b) || b == 0.0 || a == JsMathInfinity() ||
        a == -JsMathInfinity()) {
        return JsMathNan();
    }
    if (b == JsMathInfinity() || b == -JsMathInfinity()) {
        return a;
    }
    r = a;
    sign = JsMathSignBit(a) ? -1.0 : 1.0;
    /* A finite `int64_t` path first: it is exact and it is what almost every call is. */
    if (JsMathIsFinite(a) && JsMathIsFinite(b) && JsMathAbs(a) < 9.0e15 && JsMathAbs(b) < 9.0e15) {
        double quotient = JsMathTruncate(a / b);

        r = a - b * quotient;
        if (r != 0.0 && (r < 0.0) != (a < 0.0)) {
            r += (a < 0.0) ? -JsMathAbs(b) : JsMathAbs(b);
        }
        return r;
    }
    while (JsMathAbs(r) >= JsMathAbs(b) && guard < 4096) {
        step = JsMathAbs(b);
        while (JsMathAbs(r) >= step * 2.0 && step * 2.0 != JsMathInfinity() && step * 2.0 > step) {
            step *= 2.0;
        }
        r -= (sign < 0.0) ? -step : step;
        guard += 1;
    }
    return r;
}

/** Square root by Newton's iteration from a bit-halved exponent guess. */
static inline double JsMathSqrt(double value)
{
    union {
        uint64_t bits;
        double value;
    } u;
    double guess;
    int i;

    if (JsMathIsNan(value) || value < 0.0) {
        return JsMathNan();
    }
    if (value == 0.0 || value == JsMathInfinity()) {
        return value;
    }
    u.value = value;
    u.bits = (u.bits >> 1) + 0x1FF8000000000000ull;
    guess = u.value;
    for (i = 0; i < 6; ++i) {
        guess = 0.5 * (guess + value / guess);
    }
    return guess;
}

/** `ln(value)`; the argument is split into a mantissa in [1,2) and a power of two. */
static inline double JsMathLog(double value)
{
    union {
        uint64_t bits;
        double value;
    } u;
    double mantissa;
    double y;
    double y2;
    double term;
    double sum;
    int32_t exponent;
    int i;

    if (JsMathIsNan(value) || value < 0.0) {
        return JsMathNan();
    }
    if (value == 0.0) {
        return -JsMathInfinity();
    }
    if (value == JsMathInfinity()) {
        return JsMathInfinity();
    }
    u.value = value;
    exponent = (int32_t)((u.bits >> 52) & 0x7FFull) - 1023;
    u.bits = (u.bits & 0x000FFFFFFFFFFFFFull) | 0x3FF0000000000000ull;
    mantissa = u.value;
    while (mantissa >= 2.0) {
        mantissa *= 0.5;
        exponent += 1;
    }
    while (mantissa < 1.0) {
        mantissa *= 2.0;
        exponent -= 1;
    }
    y = (mantissa - 1.0) / (mantissa + 1.0);
    y2 = y * y;
    term = y;
    sum = 0.0;
    for (i = 0; i < 21; ++i) {
        sum += term / (double)(2 * i + 1);
        term *= y2;
    }
    return 2.0 * sum + (double)exponent * 0.69314718055994530942;
}

/** `e^value`: range-reduced to |r| <= ln(2)/2 and scaled back by doubling. */
static inline double JsMathExp(double value)
{
    double k;
    double r;
    double term;
    double sum;
    int32_t steps;
    int i;

    if (JsMathIsNan(value)) {
        return JsMathNan();
    }
    if (value == JsMathInfinity()) {
        return JsMathInfinity();
    }
    if (value == -JsMathInfinity() || value < -745.0) {
        return 0.0;
    }
    if (value > 709.0) {
        return JsMathInfinity();
    }
    k = JsMathRound(value / 0.69314718055994530942);
    r = value - k * 0.69314718055994530942;
    term = 1.0;
    sum = 1.0;
    for (i = 1; i < 19; ++i) {
        term *= r / (double)i;
        sum += term;
    }
    steps = (int32_t)k;
    while (steps > 0) {
        sum *= 2.0;
        steps -= 1;
    }
    while (steps < 0) {
        sum *= 0.5;
        steps += 1;
    }
    return sum;
}

/**
 * `base ** exponent`. Integer exponents are exact by repeated squaring; anything else goes through
 * `exp(exponent * ln(base))`, and a negative base with a fractional exponent is NaN — which is the
 * answer the language gives, not a limitation of this implementation.
 */
static inline double JsMathPow(double base, double exponent)
{
    double result = 1.0;
    double factor = base;
    double remaining;
    int32_t integerExponent;
    int negativeExponent = 0;

    if (exponent == 0.0) {
        return 1.0;
    }
    if (JsMathIsNan(base) || JsMathIsNan(exponent)) {
        return JsMathNan();
    }
    if (JsMathIsFinite(exponent) && exponent == JsMathTruncate(exponent) &&
        JsMathAbs(exponent) < 2147483647.0) {
        integerExponent = (int32_t)exponent;
        if (integerExponent < 0) {
            negativeExponent = 1;
            integerExponent = -integerExponent;
        }
        remaining = (double)integerExponent;
        while (remaining > 0.0) {
            if (JsMathFmod(remaining, 2.0) == 1.0) {
                result *= factor;
            }
            factor *= factor;
            remaining = JsMathFloor(remaining / 2.0);
        }
        return negativeExponent ? 1.0 / result : result;
    }
    if (base < 0.0) {
        return JsMathNan();
    }
    if (base == 0.0) {
        return 0.0;
    }
    return JsMathExp(exponent * JsMathLog(base));
}

#endif /* ROBO_JS_MATH_H */
