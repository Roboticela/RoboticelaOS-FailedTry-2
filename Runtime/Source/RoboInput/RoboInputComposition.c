/*
 * RoboInput composition payload (Phase 13.37) — see `RoboInputComposition.h`.
 *
 * The record is one bounded preedit, a clause range and a caret, and every rule here is about the four
 * agreeing with one another:
 *
 *   the caret lies **inside** the preedit (at its end at most, which is where it is when the user has
 *   typed everything);
 *
 *   the clause is either absent -- both offsets zero -- or a range that lies inside the preedit and does
 *   not invert. An inverted range is refused rather than swapped: a caller that passed them the wrong way
 *   round has a bug, and swapping would hide it;
 *
 *   `EMPTY` agrees with the preedit being empty, in **both** directions, which is what makes the flag worth
 *   having: a consumer that switches on it and a consumer that measures the string cannot disagree.
 *
 * And one rule from the module's oldest decision: a preedit longer than the event can carry is refused,
 * never truncated, because a truncated preedit is different text.
 */

#include "RoboInput/RoboInputComposition.h"

#include "RoboInputInternal.h"

#include <stddef.h>
#include <stdint.h>

void InputCompositionDataInit(InputCompositionData *data)
{
    if (!data) {
        return;
    }
    data->preedit[0] = '\0';
    data->clauseStart = 0u;
    data->clauseEnd = 0u;
    data->caret = 0u;
    /* A cleared record **is** empty, so the derived flag is set rather than clear: the two answers a
     * consumer could compute from it agree from the first byte. */
    data->flags = (uint32_t)RoboInputCompositionFlagEmpty;
}

Result InputCompositionDataSet(InputCompositionData *data,
                               const char *preedit,
                               uint32_t clauseStart,
                               uint32_t clauseEnd,
                               uint32_t caret,
                               uint32_t flags,
                               InputDiag *diag)
{
    size_t length = 0u;

    if (!data) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "nowhere to put the composition");
    }
    InputCompositionDataInit(data);
    if ((flags & ~(uint32_t)RoboInputCompositionFlagAll) != 0u) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "composition flags carry a bit this build does not define");
    }
    if (!RiTextFits(preedit, RoboInputCompositionTextMax)) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "the preedit is longer than one composition event may carry; it is refused "
                             "rather than truncated, because a truncated preedit is different text");
    }
    length = RiBoundedStrlen(preedit, RoboInputCompositionTextMax);

    if (caret > (uint32_t)length) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "the caret is past the end of the preedit");
    }
    if (clauseStart > clauseEnd) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "the clause range is inverted");
    }
    if (clauseEnd > (uint32_t)length) {
        return RoboInputDiag(diag, RoboInputDomainEvent, RoboInvalidArgument,
                             "the clause extends past the end of the preedit");
    }

    RiTextCopy(data->preedit, RoboInputCompositionTextMax, preedit);
    data->clauseStart = clauseStart;
    data->clauseEnd = clauseEnd;
    data->caret = caret;
    data->flags = flags & ~(uint32_t)RoboInputCompositionFlagEmpty;
    if (length == 0u) {
        data->flags |= (uint32_t)RoboInputCompositionFlagEmpty;
    }
    return RoboOk;
}

int InputCompositionDataIsValid(const InputCompositionData *data)
{
    size_t length;
    int flaggedEmpty;

    if (!data) {
        return 0;
    }
    if ((data->flags & ~(uint32_t)RoboInputCompositionFlagAll) != 0u) {
        return 0;
    }
    if (!RiTextFits(data->preedit, RoboInputCompositionTextMax)) {
        return 0;
    }
    length = RiBoundedStrlen(data->preedit, RoboInputCompositionTextMax);
    flaggedEmpty = (data->flags & (uint32_t)RoboInputCompositionFlagEmpty) != 0u;
    if (flaggedEmpty != (length == 0u)) {
        return 0;
    }
    if (data->caret > (uint32_t)length) {
        return 0;
    }
    if (data->clauseStart > data->clauseEnd) {
        return 0;
    }
    if (data->clauseEnd > (uint32_t)length) {
        return 0;
    }
    return 1;
}

size_t InputCompositionDataLength(const InputCompositionData *data)
{
    if (!data) {
        return 0u;
    }
    return RiBoundedStrlen(data->preedit, RoboInputCompositionTextMax);
}

int InputCompositionDataIsEmpty(const InputCompositionData *data)
{
    return InputCompositionDataLength(data) == 0u;
}

size_t InputCompositionDataText(const InputCompositionData *data, char *out, size_t capacity)
{
    size_t length;

    if (!out || capacity == 0u) {
        return 0u;
    }
    out[0] = '\0';
    if (!data) {
        return 0u;
    }
    length = RiBoundedStrlen(data->preedit, RoboInputCompositionTextMax);
    RiTextCopy(out, capacity, data->preedit);
    if (length >= capacity) {
        return capacity - 1u;
    }
    return length;
}

int InputCompositionDataEqual(const InputCompositionData *a, const InputCompositionData *b)
{
    size_t length;
    size_t index;

    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    if (a->clauseStart != b->clauseStart || a->clauseEnd != b->clauseEnd ||
        a->caret != b->caret || a->flags != b->flags) {
        return 0;
    }
    length = RiBoundedStrlen(a->preedit, RoboInputCompositionTextMax);
    if (length != RiBoundedStrlen(b->preedit, RoboInputCompositionTextMax)) {
        return 0;
    }
    for (index = 0u; index <= length; index++) {
        if (a->preedit[index] != b->preedit[index]) {
            return 0;
        }
    }
    return 1;
}
