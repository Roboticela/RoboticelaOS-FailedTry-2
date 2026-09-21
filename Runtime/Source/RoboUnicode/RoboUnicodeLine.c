/*
 * Unicode Line Breaking — UAX #14 (Unicode 16.0.0).
 *
 * Port of Tools/ucd/lb_ref.py (passes LineBreakTest-16.0.0).
 */

#include "RoboUnicode/RoboUnicodeLine.h"

#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicodeProps.h"

#include <string.h>

#define RoboLbHyphenCp ((Uchar32)0x2010u)
#define RoboLbDottedCircle ((Uchar32)0x25CCu)
#define RoboLbClsStack 256u
#define RoboLbLookahead 64u

const char *UnicodeLineUcdVersionString(void)
{
    return RoboLineUcdVersion;
}

UnicodeLineBreakClass UnicodeLineBreakClassRaw(Uchar32 cp)
{
    return LineBreakPropertyRaw(cp);
}

UnicodeLineBreakClass MakeUnicodeLineBreakClass(Uchar32 cp)
{
    UnicodeLineBreakClass raw = LineBreakPropertyRaw(cp);
    UnicodeGeneralCategory gc;

    if (raw == RoboULbAi || raw == RoboULbSg || raw == RoboULbXx) {
        return RoboULbAl;
    }
    if (raw == RoboULbSa) {
        gc = UnicodeCategory(cp);
        return (gc == RoboUGcMn || gc == RoboUGcMc) ? RoboULbCm
                                                         : RoboULbAl;
    }
    if (raw == RoboULbCj) {
        return RoboULbNs;
    }
    return raw;
}

static int LbIsCm(UnicodeLineBreakClass c)
{
    return c == RoboULbCm || c == RoboULbZwj;
}

static int LbNoBreakCmBase(UnicodeLineBreakClass c)
{
    return c == RoboULbBk || c == RoboULbCr || c == RoboULbLf ||
           c == RoboULbNl || c == RoboULbSp || c == RoboULbZw;
}

static int LbHardBreakAfter(UnicodeLineBreakClass c)
{
    return c == RoboULbBk || c == RoboULbCr || c == RoboULbLf ||
           c == RoboULbNl;
}

static int LbIsEastAsian(Uchar32 cp)
{
    UnicodeEastAsianWidth w = MakeUnicodeEastAsianWidth(cp);
    return w == RoboUEawFullwidth || w == RoboUEawWide ||
           w == RoboUEawHalfwidth;
}

static int LbIsQuPi(Uchar32 cp)
{
    return MakeUnicodeLineBreakClass(cp) == RoboULbQu &&
           UnicodeCategory(cp) == RoboUGcPi;
}

static int LbIsQuPf(Uchar32 cp)
{
    return MakeUnicodeLineBreakClass(cp) == RoboULbQu &&
           UnicodeCategory(cp) == RoboUGcPf;
}

static int LbIsExtPictCn(Uchar32 cp)
{
    return UnicodeIsExtendedPictographic(cp) &&
           UnicodeCategory(cp) == RoboUGcCn;
}

/* Skip trailing CM/ZWJ from j; return base index or (size_t)-1. */
static size_t LbSkipCmBack(const UnicodeLineBreakClass *cls,
                                   size_t j)
{
    while (j != (size_t)-1 && LbIsCm(cls[j])) {
        if (j == 0u) {
            return (size_t)-1;
        }
        j--;
    }
    return j;
}

/*
 * Effective class before break position i (LB9/LB10).
 * Returns 0 if sot; else 1 and sets *outC / *outI.
 */
static int LbLeftBase(const UnicodeLineBreakClass *cls,
                             size_t i,
                             UnicodeLineBreakClass *outC,
                             size_t *outI)
{
    size_t j;
    size_t k;

    if (i == 0u) {
        return 0;
    }
    j = i - 1u;
    if (LbIsCm(cls[j])) {
        k = LbSkipCmBack(cls, j);
        if (k != (size_t)-1 && !LbNoBreakCmBase(cls[k])) {
            *outC = cls[k];
            *outI = k;
            return 1;
        }
        *outC = RoboULbAl; /* LB10 orphan */
        *outI = j;
        return 1;
    }
    *outC = cls[j];
    *outI = j;
    return 1;
}

static int LbRightEff(const UnicodeLineBreakClass *cls,
                             size_t len,
                             size_t i,
                             UnicodeLineBreakClass *outC)
{
    if (i >= len) {
        return 0;
    }
    if (LbIsCm(cls[i])) {
        *outC = RoboULbAl; /* LB10 */
        return 1;
    }
    *outC = cls[i];
    return 1;
}

static int LbMatchZwSpStarDiv(const UnicodeLineBreakClass *cls,
                                        size_t i)
{
    size_t j;

    if (i == 0u) {
        return 0;
    }
    j = i - 1u;
    while (j != (size_t)-1 && cls[j] == RoboULbSp) {
        if (j == 0u) {
            return 0;
        }
        j--;
    }
    return j != (size_t)-1 && cls[j] == RoboULbZw;
}

static int LbOpSpStar(const UnicodeLineBreakClass *cls, size_t i)
{
    size_t j;
    UnicodeLineBreakClass c = RoboULbXx;
    size_t bi = 0;

    if (i == 0u) {
        return 0;
    }
    j = i - 1u;
    while (j != (size_t)-1 && cls[j] == RoboULbSp) {
        if (j == 0u) {
            return 0;
        }
        j--;
    }
    if (j == (size_t)-1) {
        return 0;
    }
    if (!LbLeftBase(cls, j + 1u, &c, &bi)) {
        return 0;
    }
    return c == RoboULbOp;
}

static int LbClCpSpStar(const UnicodeLineBreakClass *cls,
                                 size_t i)
{
    size_t j;
    UnicodeLineBreakClass c = RoboULbXx;
    size_t bi = 0;

    if (i == 0u) {
        return 0;
    }
    j = i - 1u;
    while (j != (size_t)-1 && cls[j] == RoboULbSp) {
        if (j == 0u) {
            return 0;
        }
        j--;
    }
    if (j == (size_t)-1) {
        return 0;
    }
    if (!LbLeftBase(cls, j + 1u, &c, &bi)) {
        return 0;
    }
    return c == RoboULbCl || c == RoboULbCp;
}

static int LbB2SpStar(const UnicodeLineBreakClass *cls, size_t i)
{
    size_t j;
    UnicodeLineBreakClass c = RoboULbXx;
    size_t bi = 0;

    if (i == 0u) {
        return 0;
    }
    j = i - 1u;
    while (j != (size_t)-1 && cls[j] == RoboULbSp) {
        if (j == 0u) {
            return 0;
        }
        j--;
    }
    if (j == (size_t)-1) {
        return 0;
    }
    if (!LbLeftBase(cls, j + 1u, &c, &bi)) {
        return 0;
    }
    return c == RoboULbB2;
}

static int Lb15a(const UnicodeLineBreakClass *cls,
                      const Uchar32 *cps,
                      size_t i)
{
    size_t j;
    size_t quIdx;
    UnicodeLineBreakClass beforeC = RoboULbXx;
    size_t bi = 0;

    if (i == 0u) {
        return 0;
    }
    j = i - 1u;
    while (j != (size_t)-1 && cls[j] == RoboULbSp) {
        if (j == 0u) {
            return 0;
        }
        j--;
    }
    if (j == (size_t)-1) {
        return 0;
    }
    if (LbIsCm(cls[j])) {
        size_t k = LbSkipCmBack(cls, j);
        if (k == (size_t)-1 || LbNoBreakCmBase(cls[k])) {
            return 0;
        }
        quIdx = k;
    } else {
        quIdx = j;
    }
    if (!LbIsQuPi(cps[quIdx])) {
        return 0;
    }
    if (!LbLeftBase(cls, quIdx, &beforeC, &bi)) {
        return 1; /* sot */
    }
    return beforeC == RoboULbBk || beforeC == RoboULbCr ||
           beforeC == RoboULbLf || beforeC == RoboULbNl ||
           beforeC == RoboULbOp || beforeC == RoboULbQu ||
           beforeC == RoboULbGl || beforeC == RoboULbSp ||
           beforeC == RoboULbZw;
}

static int Lb15b(const UnicodeLineBreakClass *cls,
                      const Uchar32 *cps,
                      size_t len,
                      size_t i)
{
    size_t k;
    UnicodeLineBreakClass fol;

    if (i >= len) {
        return 0;
    }
    if (!LbIsQuPf(cps[i])) {
        return 0;
    }
    k = i + 1u;
    while (k < len && LbIsCm(cls[k])) {
        k++;
    }
    if (k >= len) {
        return 1; /* eot */
    }
    fol = cls[k];
    return fol == RoboULbSp || fol == RoboULbGl || fol == RoboULbWj ||
           fol == RoboULbCl || fol == RoboULbQu || fol == RoboULbCp ||
           fol == RoboULbEx || fol == RoboULbIs || fol == RoboULbSy ||
           fol == RoboULbBk || fol == RoboULbCr || fol == RoboULbLf ||
           fol == RoboULbNl || fol == RoboULbZw;
}

static int LbRiOddPair(const UnicodeLineBreakClass *cls,
                               size_t i)
{
    size_t count = 0;
    size_t j;

    if (i == 0u) {
        return 0;
    }
    j = i - 1u;
    while (j != (size_t)-1) {
        if (LbIsCm(cls[j])) {
            size_t k = LbSkipCmBack(cls, j);
            if (k == (size_t)-1 || LbNoBreakCmBase(cls[k])) {
                break;
            }
            if (cls[k] != RoboULbRi) {
                break;
            }
            count++;
            if (k == 0u) {
                break;
            }
            j = k - 1u;
            continue;
        }
        if (cls[j] != RoboULbRi) {
            break;
        }
        count++;
        if (j == 0u) {
            break;
        }
        j--;
    }
    return (count % 2u) == 1u;
}

static int LbNuSyIsStarBefore(const UnicodeLineBreakClass *cls,
                                        size_t i)
{
    size_t j;

    if (i == 0u) {
        return 0;
    }
    j = i - 1u;
    while (j != (size_t)-1) {
        if (LbIsCm(cls[j])) {
            size_t k = LbSkipCmBack(cls, j);
            if (k == (size_t)-1 || LbNoBreakCmBase(cls[k])) {
                return 0;
            }
            if (cls[k] == RoboULbSy || cls[k] == RoboULbIs) {
                if (k == 0u) {
                    return 0;
                }
                j = k - 1u;
                continue;
            }
            if (cls[k] == RoboULbNu) {
                return 1;
            }
            return 0;
        }
        if (cls[j] == RoboULbSy || cls[j] == RoboULbIs) {
            if (j == 0u) {
                return 0;
            }
            j--;
            continue;
        }
        if (cls[j] == RoboULbNu) {
            return 1;
        }
        return 0;
    }
    return 0;
}

static int LbNuSyIsStarClCpBefore(
    const UnicodeLineBreakClass *cls,
    size_t i,
    UnicodeLineBreakClass which)
{
    size_t j;

    if (i == 0u) {
        return 0;
    }
    j = i - 1u;
    if (LbIsCm(cls[j])) {
        size_t k = LbSkipCmBack(cls, j);
        if (k == (size_t)-1 || LbNoBreakCmBase(cls[k])) {
            return 0;
        }
        if (cls[k] != which) {
            return 0;
        }
        return LbNuSyIsStarBefore(cls, k);
    }
    if (cls[j] != which) {
        return 0;
    }
    return LbNuSyIsStarBefore(cls, j);
}

static int Lb20a(const UnicodeLineBreakClass *cls,
                      const Uchar32 *cps,
                      size_t len,
                      size_t i)
{
    UnicodeLineBreakClass right = RoboULbXx;
    UnicodeLineBreakClass leftC = RoboULbXx;
    UnicodeLineBreakClass beforeC = RoboULbXx;
    size_t leftI = 0;
    size_t bi = 0;

    if (!LbRightEff(cls, len, i, &right) || right != RoboULbAl) {
        return 0;
    }
    if (!LbLeftBase(cls, i, &leftC, &leftI)) {
        return 0;
    }
    if (leftC == RoboULbHy) {
        /* ok */
    } else if (leftC == RoboULbBa && cps[leftI] == RoboLbHyphenCp) {
        /* ok */
    } else {
        return 0;
    }
    if (!LbLeftBase(cls, leftI, &beforeC, &bi)) {
        return 1;
    }
    return beforeC == RoboULbBk || beforeC == RoboULbCr ||
           beforeC == RoboULbLf || beforeC == RoboULbNl ||
           beforeC == RoboULbSp || beforeC == RoboULbZw ||
           beforeC == RoboULbCb || beforeC == RoboULbGl;
}

static int LbIsAkAsOrDotted(const UnicodeLineBreakClass *cls,
                                      const Uchar32 *cps,
                                      size_t len,
                                      size_t idx,
                                      UnicodeLineBreakClass c)
{
    if (c == RoboULbAk || c == RoboULbAs) {
        return 1;
    }
    return idx < len && !LbIsCm(cls[idx]) &&
           cps[idx] == RoboLbDottedCircle;
}

static int Lb28a(const UnicodeLineBreakClass *cls,
                      const Uchar32 *cps,
                      size_t len,
                      size_t i)
{
    UnicodeLineBreakClass left = RoboULbXx;
    UnicodeLineBreakClass right = RoboULbXx;
    UnicodeLineBreakClass before = RoboULbXx;
    size_t li = 0;
    size_t bi = 0;
    size_t k;

    if (!LbLeftBase(cls, i, &left, &li)) {
        return 0;
    }
    if (!LbRightEff(cls, len, i, &right)) {
        return 0;
    }

    if (left == RoboULbAp &&
        LbIsAkAsOrDotted(cls, cps, len, i, right)) {
        return 1;
    }

    if (LbIsAkAsOrDotted(cls, cps, len, li, left) &&
        (right == RoboULbVf || right == RoboULbVi)) {
        return 1;
    }

    if (left == RoboULbVi) {
        if (LbLeftBase(cls, li, &before, &bi) &&
            LbIsAkAsOrDotted(cls, cps, len, bi, before)) {
            if (right == RoboULbAk ||
                (i < len && !LbIsCm(cls[i]) &&
                 cps[i] == RoboLbDottedCircle)) {
                return 1;
            }
        }
    }

    if (LbIsAkAsOrDotted(cls, cps, len, li, left) &&
        LbIsAkAsOrDotted(cls, cps, len, i, right)) {
        k = i + 1u;
        while (k < len && LbIsCm(cls[k])) {
            k++;
        }
        if (k < len && cls[k] == RoboULbVf) {
            return 1;
        }
    }

    return 0;
}

static int Lb25(const UnicodeLineBreakClass *cls,
                     size_t len,
                     size_t i,
                     UnicodeLineBreakClass left,
                     UnicodeLineBreakClass right)
{
    size_t k;
    size_t k2;

    if (right == RoboULbPo || right == RoboULbPr || right == RoboULbNu) {
        if (LbNuSyIsStarClCpBefore(cls, i, RoboULbCl) &&
            (right == RoboULbPo || right == RoboULbPr)) {
            return 1;
        }
        if (LbNuSyIsStarClCpBefore(cls, i, RoboULbCp) &&
            (right == RoboULbPo || right == RoboULbPr)) {
            return 1;
        }
        if (LbNuSyIsStarBefore(cls, i) &&
            (right == RoboULbPo || right == RoboULbPr ||
             right == RoboULbNu)) {
            return 1;
        }
    }

    if (left == RoboULbPo) {
        if (right == RoboULbNu) {
            return 1;
        }
        if (right == RoboULbOp) {
            k = i + 1u;
            while (k < len && LbIsCm(cls[k])) {
                k++;
            }
            if (k < len && cls[k] == RoboULbNu) {
                return 1;
            }
            if (k < len && cls[k] == RoboULbIs) {
                k2 = k + 1u;
                while (k2 < len && LbIsCm(cls[k2])) {
                    k2++;
                }
                if (k2 < len && cls[k2] == RoboULbNu) {
                    return 1;
                }
            }
        }
    }

    if (left == RoboULbPr) {
        if (right == RoboULbNu) {
            return 1;
        }
        if (right == RoboULbOp) {
            k = i + 1u;
            while (k < len && LbIsCm(cls[k])) {
                k++;
            }
            if (k < len && cls[k] == RoboULbNu) {
                return 1;
            }
            if (k < len && cls[k] == RoboULbIs) {
                k2 = k + 1u;
                while (k2 < len && LbIsCm(cls[k2])) {
                    k2++;
                }
                if (k2 < len && cls[k2] == RoboULbNu) {
                    return 1;
                }
            }
        }
    }

    if (left == RoboULbHy && right == RoboULbNu) {
        return 1;
    }
    if (left == RoboULbIs && right == RoboULbNu) {
        return 1;
    }
    return 0;
}

static UnicodeLineBreakOp LbOpportunityWithCls(
    const Uchar32 *cps,
    const UnicodeLineBreakClass *cls,
    size_t len,
    size_t index)
{
    UnicodeLineBreakClass leftRaw;
    UnicodeLineBreakClass rightRaw;
    UnicodeLineBreakClass left = RoboULbXx;
    UnicodeLineBreakClass right = RoboULbXx;
    UnicodeLineBreakClass before = RoboULbXx;
    size_t leftI = 0;
    size_t bi = 0;
    size_t k;

    if (index == 0u) {
        return RoboLineBreakProhibited; /* LB2 */
    }
    if (index >= len) {
        return RoboLineBreakMandatory; /* LB3 */
    }

    leftRaw = cls[index - 1u];
    rightRaw = cls[index];

    if (leftRaw == RoboULbBk) {
        return RoboLineBreakMandatory;
    }
    if (leftRaw == RoboULbCr && rightRaw == RoboULbLf) {
        return RoboLineBreakProhibited;
    }
    if (leftRaw == RoboULbCr || leftRaw == RoboULbLf ||
        leftRaw == RoboULbNl) {
        return RoboLineBreakMandatory;
    }
    if (LbHardBreakAfter(rightRaw)) {
        return RoboLineBreakProhibited;
    }
    if (rightRaw == RoboULbSp || rightRaw == RoboULbZw) {
        return RoboLineBreakProhibited;
    }
    if (LbMatchZwSpStarDiv(cls, index)) {
        return RoboLineBreakAllowed;
    }
    if (leftRaw == RoboULbZwj) {
        return RoboLineBreakProhibited;
    }
    if (LbIsCm(rightRaw) && !LbNoBreakCmBase(leftRaw)) {
        return RoboLineBreakProhibited;
    }

    (void)LbLeftBase(cls, index, &left, &leftI);
    (void)LbRightEff(cls, len, index, &right);

    if (right == RoboULbWj || left == RoboULbWj) {
        return RoboLineBreakProhibited;
    }
    if (left == RoboULbGl) {
        return RoboLineBreakProhibited;
    }
    if (right == RoboULbGl && left != RoboULbSp && left != RoboULbBa &&
        left != RoboULbHy) {
        return RoboLineBreakProhibited;
    }
    if (right == RoboULbCl || right == RoboULbCp || right == RoboULbEx ||
        right == RoboULbSy) {
        return RoboLineBreakProhibited;
    }
    if (LbOpSpStar(cls, index)) {
        return RoboLineBreakProhibited;
    }
    if (Lb15a(cls, cps, index)) {
        return RoboLineBreakProhibited;
    }
    if (Lb15b(cls, cps, len, index)) {
        return RoboLineBreakProhibited;
    }
    if (leftRaw == RoboULbSp && right == RoboULbIs) {
        k = index + 1u;
        while (k < len && LbIsCm(cls[k])) {
            k++;
        }
        if (k < len && cls[k] == RoboULbNu) {
            return RoboLineBreakAllowed;
        }
    }
    if (right == RoboULbIs) {
        return RoboLineBreakProhibited;
    }
    if (right == RoboULbNs && LbClCpSpStar(cls, index)) {
        return RoboLineBreakProhibited;
    }
    if (right == RoboULbB2 && LbB2SpStar(cls, index)) {
        return RoboLineBreakProhibited;
    }
    if (leftRaw == RoboULbSp) {
        return RoboLineBreakAllowed;
    }

    if (right == RoboULbQu && !LbIsQuPi(cps[index])) {
        return RoboLineBreakProhibited;
    }
    if (left == RoboULbQu && !LbIsQuPf(cps[leftI])) {
        return RoboLineBreakProhibited;
    }

    if (right == RoboULbQu && !LbIsEastAsian(cps[leftI])) {
        return RoboLineBreakProhibited;
    }
    if (right == RoboULbQu) {
        k = index + 1u;
        while (k < len && LbIsCm(cls[k])) {
            k++;
        }
        if (k >= len || !LbIsEastAsian(cps[k])) {
            return RoboLineBreakProhibited;
        }
    }
    if (left == RoboULbQu) {
        if (LbIsCm(cls[index]) || !LbIsEastAsian(cps[index])) {
            return RoboLineBreakProhibited;
        }
    }
    if (left == RoboULbQu) {
        if (!LbLeftBase(cls, leftI, &before, &bi) ||
            !LbIsEastAsian(cps[bi])) {
            return RoboLineBreakProhibited;
        }
    }

    if (right == RoboULbCb || left == RoboULbCb) {
        return RoboLineBreakAllowed;
    }

    if (Lb20a(cls, cps, len, index)) {
        return RoboLineBreakProhibited;
    }

    if (right == RoboULbBa || right == RoboULbHy || right == RoboULbNs) {
        return RoboLineBreakProhibited;
    }
    if (left == RoboULbBb) {
        return RoboLineBreakProhibited;
    }

    if ((left == RoboULbHy || left == RoboULbBa) && right != RoboULbHl) {
        if (!(left == RoboULbBa && LbIsEastAsian(cps[leftI]))) {
            if (LbLeftBase(cls, leftI, &before, &bi) &&
                before == RoboULbHl) {
                return RoboLineBreakProhibited;
            }
        }
    }

    if (left == RoboULbSy && right == RoboULbHl) {
        return RoboLineBreakProhibited;
    }

    if (right == RoboULbIn) {
        return RoboLineBreakProhibited;
    }

    if ((left == RoboULbAl || left == RoboULbHl) && right == RoboULbNu) {
        return RoboLineBreakProhibited;
    }
    if (left == RoboULbNu &&
        (right == RoboULbAl || right == RoboULbHl)) {
        return RoboLineBreakProhibited;
    }

    if (left == RoboULbPr &&
        (right == RoboULbId || right == RoboULbEb ||
         right == RoboULbEm)) {
        return RoboLineBreakProhibited;
    }
    if ((left == RoboULbId || left == RoboULbEb || left == RoboULbEm) &&
        right == RoboULbPo) {
        return RoboLineBreakProhibited;
    }

    if ((left == RoboULbPr || left == RoboULbPo) &&
        (right == RoboULbAl || right == RoboULbHl)) {
        return RoboLineBreakProhibited;
    }
    if ((left == RoboULbAl || left == RoboULbHl) &&
        (right == RoboULbPr || right == RoboULbPo)) {
        return RoboLineBreakProhibited;
    }

    if (Lb25(cls, len, index, left, right)) {
        return RoboLineBreakProhibited;
    }

    if (left == RoboULbJl &&
        (right == RoboULbJl || right == RoboULbJv ||
         right == RoboULbH2 || right == RoboULbH3)) {
        return RoboLineBreakProhibited;
    }
    if ((left == RoboULbJv || left == RoboULbH2) &&
        (right == RoboULbJv || right == RoboULbJt)) {
        return RoboLineBreakProhibited;
    }
    if ((left == RoboULbJt || left == RoboULbH3) &&
        right == RoboULbJt) {
        return RoboLineBreakProhibited;
    }

    if ((left == RoboULbJl || left == RoboULbJv || left == RoboULbJt ||
         left == RoboULbH2 || left == RoboULbH3) &&
        right == RoboULbPo) {
        return RoboLineBreakProhibited;
    }
    if (left == RoboULbPr &&
        (right == RoboULbJl || right == RoboULbJv ||
         right == RoboULbJt || right == RoboULbH2 ||
         right == RoboULbH3)) {
        return RoboLineBreakProhibited;
    }

    if ((left == RoboULbAl || left == RoboULbHl) &&
        (right == RoboULbAl || right == RoboULbHl)) {
        return RoboLineBreakProhibited;
    }

    if (Lb28a(cls, cps, len, index)) {
        return RoboLineBreakProhibited;
    }

    if (left == RoboULbIs &&
        (right == RoboULbAl || right == RoboULbHl)) {
        return RoboLineBreakProhibited;
    }

    if ((left == RoboULbAl || left == RoboULbHl || left == RoboULbNu) &&
        right == RoboULbOp && !LbIsEastAsian(cps[index])) {
        return RoboLineBreakProhibited;
    }
    if (left == RoboULbCp && !LbIsEastAsian(cps[leftI]) &&
        (right == RoboULbAl || right == RoboULbHl ||
         right == RoboULbNu)) {
        return RoboLineBreakProhibited;
    }

    if (left == RoboULbRi && right == RoboULbRi &&
        LbRiOddPair(cls, index)) {
        return RoboLineBreakProhibited;
    }

    if (left == RoboULbEb && right == RoboULbEm) {
        return RoboLineBreakProhibited;
    }
    if (right == RoboULbEm && LbIsExtPictCn(cps[leftI])) {
        return RoboLineBreakProhibited;
    }

    return RoboLineBreakAllowed;
}

UnicodeLineBreakOp UnicodeLineBreakOpportunity(
    const Uchar32 *cps, size_t len, size_t index)
{
    UnicodeLineBreakClass stackCls[RoboLbClsStack];
    UnicodeLineBreakClass *cls;
    UnicodeLineBreakOp op;
    size_t i;
    int heap = 0;

    if (index == 0u) {
        return RoboLineBreakProhibited;
    }
    if (index >= len || !cps) {
        return RoboLineBreakMandatory;
    }

    if (len <= RoboLbClsStack) {
        cls = stackCls;
    } else {
        cls = (UnicodeLineBreakClass *)Malloc(
            len * sizeof(UnicodeLineBreakClass));
        if (!cls) {
            return RoboLineBreakAllowed;
        }
        heap = 1;
    }
    for (i = 0; i < len; i++) {
        cls[i] = MakeUnicodeLineBreakClass(cps[i]);
    }
    op = LbOpportunityWithCls(cps, cls, len, index);
    if (heap) {
        RoboFree(cls);
    }
    return op;
}

int UnicodeLineBreak(const Uchar32 *cps, size_t len, size_t index)
{
    return UnicodeLineBreakOpportunity(cps, len, index) !=
           RoboLineBreakProhibited;
}

static Result LineDecodeAt(const char *src,
                                         size_t srcLen,
                                         size_t offset,
                                         UnicodeMode mode,
                                         Uchar32 *cp,
                                         size_t *byteEnd)
{
    size_t next = offset;
    UnicodeStatus st =
        UnicodeUtf8Decode(src, srcLen, offset, mode, cp, &next);
    if (st != RoboUnicodeOk) {
        return RoboParseError;
    }
    *byteEnd = next;
    return RoboOk;
}

static Result LineNextSegment(const char *src,
                                            size_t srcLen,
                                            size_t byteOffset,
                                            UnicodeMode mode,
                                            size_t *lineStart,
                                            size_t *lineEnd,
                                            UnicodeLineBreakOp *op)
{
    Uchar32 stackCps[96];
    size_t stackEnds[96];
    Uchar32 *cps = stackCps;
    size_t *cpEnds = stackEnds;
    size_t cap = 96u;
    size_t n = 0;
    size_t off = byteOffset;
    UnicodeLineBreakOp lastOp = RoboLineBreakMandatory;
    Result r;

    if (byteOffset >= srcLen) {
        *lineStart = srcLen;
        *lineEnd = srcLen;
        if (op) {
            *op = RoboLineBreakProhibited;
        }
        return RoboOk;
    }
    *lineStart = byteOffset;

    for (;;) {
        Uchar32 cp = 0;
        size_t bend = off;
        size_t peekN;
        size_t peekOff;
        size_t i;

        if (off >= srcLen) {
            lastOp = RoboLineBreakMandatory;
            break;
        }
        r = LineDecodeAt(src, srcLen, off, mode, &cp, &bend);
        if (r != RoboOk) {
            if (cps != stackCps) {
                RoboFree(cps);
            }
            if (cpEnds != stackEnds) {
                RoboFree(cpEnds);
            }
            return r;
        }

        if (n > 0u) {
            Uchar32 peekStack[160];
            Uchar32 *peek = peekStack;
            size_t peekCap = 160u;
            int heapPeek = 0;
            UnicodeLineBreakOp kind;

            if (n + 1u + RoboLbLookahead > peekCap) {
                peekCap = n + 1u + RoboLbLookahead + 32u;
                peek = (Uchar32 *)Malloc(peekCap *
                                                     sizeof(Uchar32));
                if (!peek) {
                    if (cps != stackCps) {
                        RoboFree(cps);
                    }
                    if (cpEnds != stackEnds) {
                        RoboFree(cpEnds);
                    }
                    return RoboOutOfMemory;
                }
                heapPeek = 1;
            }
            for (i = 0; i < n; i++) {
                peek[i] = cps[i];
            }
            peek[n] = cp;
            peekN = n + 1u;
            peekOff = bend;
            while (peekN < n + 1u + RoboLbLookahead && peekOff < srcLen) {
                Uchar32 pcp = 0;
                size_t pend = peekOff;
                r = LineDecodeAt(src, srcLen, peekOff, mode, &pcp, &pend);
                if (r != RoboOk) {
                    if (heapPeek) {
                        RoboFree(peek);
                    }
                    if (cps != stackCps) {
                        RoboFree(cps);
                    }
                    if (cpEnds != stackEnds) {
                        RoboFree(cpEnds);
                    }
                    return r;
                }
                if (peekN >= peekCap) {
                    break;
                }
                peek[peekN++] = pcp;
                peekOff = pend;
            }
            kind = UnicodeLineBreakOpportunity(peek, peekN, n);
            if (kind != RoboLineBreakProhibited) {
                lastOp = kind;
                if (heapPeek) {
                    RoboFree(peek);
                }
                break;
            }
            if (heapPeek) {
                RoboFree(peek);
            }
        }

        if (n >= cap) {
            size_t ncap = cap * 2u;
            Uchar32 *nc =
                (Uchar32 *)Malloc(ncap * sizeof(Uchar32));
            size_t *ne = (size_t *)Malloc(ncap * sizeof(size_t));
            if (!nc || !ne) {
                RoboFree(nc);
                RoboFree(ne);
                if (cps != stackCps) {
                    RoboFree(cps);
                }
                if (cpEnds != stackEnds) {
                    RoboFree(cpEnds);
                }
                return RoboOutOfMemory;
            }
            memcpy(nc, cps, n * sizeof(Uchar32));
            memcpy(ne, cpEnds, n * sizeof(size_t));
            if (cps != stackCps) {
                RoboFree(cps);
            }
            if (cpEnds != stackEnds) {
                RoboFree(cpEnds);
            }
            cps = nc;
            cpEnds = ne;
            cap = ncap;
        }
        cps[n] = cp;
        cpEnds[n] = bend;
        n++;
        off = bend;
    }

    *lineEnd = (n == 0u) ? byteOffset : cpEnds[n - 1u];
    if (op) {
        *op = lastOp;
    }
    if (cps != stackCps) {
        RoboFree(cps);
    }
    if (cpEnds != stackEnds) {
        RoboFree(cpEnds);
    }
    return RoboOk;
}

Result UnicodeLineIterInit(UnicodeLineIter *it,
                                          const char *src,
                                          size_t srcLen,
                                          UnicodeMode mode)
{
    if (!it) {
        return RoboInvalidArgument;
    }
    it->src = src;
    it->srcLen = srcLen;
    it->mode = mode;
    it->byteOffset = 0;
    it->lineIndex = 0;
    it->valid = 1;
    return RoboOk;
}

Result UnicodeLineIterNext(UnicodeLineIter *it,
                                          size_t *lineStart,
                                          size_t *lineEnd,
                                          UnicodeLineBreakOp *op)
{
    Result r;
    UnicodeLineBreakOp kind = RoboLineBreakProhibited;

    if (!it || !lineStart || !lineEnd) {
        return RoboInvalidArgument;
    }
    if (!it->valid) {
        return RoboParseError;
    }
    r = LineNextSegment(it->src,
                               it->srcLen,
                               it->byteOffset,
                               it->mode,
                               lineStart,
                               lineEnd,
                               &kind);
    if (r != RoboOk) {
        it->valid = 0;
        return r;
    }
    if (op) {
        *op = kind;
    }
    it->byteOffset = *lineEnd;
    if (*lineStart < it->srcLen && *lineStart != *lineEnd) {
        it->lineIndex++;
    }
    return RoboOk;
}

Result UnicodeLineBreakNext(const char *src,
                                           size_t srcLen,
                                           size_t byteOffset,
                                           UnicodeMode mode,
                                           size_t *lineStart,
                                           size_t *lineEnd,
                                           UnicodeLineBreakOp *op)
{
    if (byteOffset > srcLen) {
        return RoboInvalidArgument;
    }
    return LineNextSegment(
        src, srcLen, byteOffset, mode, lineStart, lineEnd, op);
}

Result UnicodeLineBreakPrev(const char *src,
                                           size_t srcLen,
                                           size_t byteOffset,
                                           UnicodeMode mode,
                                           size_t *lineStart,
                                           size_t *lineEnd,
                                           UnicodeLineBreakOp *op)
{
    UnicodeLineIter it;
    size_t ls = 0;
    size_t le = 0;
    size_t lastLs = 0;
    size_t lastLe = 0;
    UnicodeLineBreakOp kind = RoboLineBreakProhibited;
    UnicodeLineBreakOp lastKind = RoboLineBreakProhibited;

    if (!lineStart || !lineEnd) {
        return RoboInvalidArgument;
    }
    if (byteOffset > srcLen) {
        return RoboInvalidArgument;
    }
    if (byteOffset == 0u) {
        *lineStart = 0;
        *lineEnd = 0;
        if (op) {
            *op = RoboLineBreakProhibited;
        }
        return RoboOk;
    }

    UnicodeLineIterInit(&it, src, srcLen, mode);
    while (it.byteOffset < byteOffset && it.valid) {
        lastLs = ls;
        lastLe = le;
        lastKind = kind;
        if (UnicodeLineIterNext(&it, &ls, &le, &kind) != RoboOk) {
            return RoboParseError;
        }
        if (le >= byteOffset) {
            if (ls < byteOffset) {
                *lineStart = ls;
                *lineEnd = le;
                if (op) {
                    *op = kind;
                }
            } else {
                *lineStart = lastLs;
                *lineEnd = lastLe;
                if (op) {
                    *op = lastKind;
                }
            }
            return RoboOk;
        }
    }
    *lineStart = lastLs;
    *lineEnd = lastLe;
    if (op) {
        *op = lastKind;
    }
    return RoboOk;
}

Result UnicodeLineBreakCountUtf8(const char *src,
                                                 size_t srcLen,
                                                 UnicodeMode mode,
                                                 size_t *count)
{
    UnicodeLineIter it;
    size_t ls = 0;
    size_t le = 0;
    size_t n = 0;
    UnicodeLineBreakOp kind = RoboLineBreakProhibited;
    Result r;

    if (!count) {
        return RoboInvalidArgument;
    }
    *count = 0;
    if (srcLen == 0u) {
        return RoboOk;
    }
    if (!src) {
        return RoboInvalidArgument;
    }

    UnicodeLineIterInit(&it, src, srcLen, mode);
    while (it.byteOffset < srcLen && it.valid) {
        r = UnicodeLineIterNext(&it, &ls, &le, &kind);
        if (r != RoboOk) {
            return r;
        }
        if (ls >= srcLen && le >= srcLen) {
            break;
        }
        n++;
    }
    *count = n;
    return RoboOk;
}
