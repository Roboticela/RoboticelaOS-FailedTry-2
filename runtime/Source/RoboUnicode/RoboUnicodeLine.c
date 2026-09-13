/*
 * Unicode Line Breaking — UAX #14 (Unicode 16.0.0).
 *
 * Port of Tools/ucd/lb_ref.py (passes LineBreakTest-16.0.0).
 */

#include "RoboUnicode/RoboUnicodeLine.h"

#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicodeProps.h"

#include <string.h>

#define ROBO_LB_HYPHEN_CP ((robo_uchar32_t)0x2010u)
#define ROBO_LB_DOTTED_CIRCLE ((robo_uchar32_t)0x25CCu)
#define ROBO_LB_CLS_STACK 256u
#define ROBO_LB_LOOKAHEAD 64u

const char *robo_unicode_line_ucd_version_string(void)
{
    return ROBO_LINE_UCD_VERSION;
}

robo_unicode_line_break_class_t robo_unicode_line_break_class_raw(robo_uchar32_t cp)
{
    return robo_line_break_property_raw(cp);
}

robo_unicode_line_break_class_t robo_unicode_line_break_class(robo_uchar32_t cp)
{
    robo_unicode_line_break_class_t raw = robo_line_break_property_raw(cp);
    robo_unicode_general_category_t gc;

    if (raw == ROBO_U_LB_AI || raw == ROBO_U_LB_SG || raw == ROBO_U_LB_XX) {
        return ROBO_U_LB_AL;
    }
    if (raw == ROBO_U_LB_SA) {
        gc = robo_unicode_category(cp);
        return (gc == ROBO_U_GC_MN || gc == ROBO_U_GC_MC) ? ROBO_U_LB_CM
                                                         : ROBO_U_LB_AL;
    }
    if (raw == ROBO_U_LB_CJ) {
        return ROBO_U_LB_NS;
    }
    return raw;
}

static int robo_lb_is_cm(robo_unicode_line_break_class_t c)
{
    return c == ROBO_U_LB_CM || c == ROBO_U_LB_ZWJ;
}

static int robo_lb_no_break_cm_base(robo_unicode_line_break_class_t c)
{
    return c == ROBO_U_LB_BK || c == ROBO_U_LB_CR || c == ROBO_U_LB_LF ||
           c == ROBO_U_LB_NL || c == ROBO_U_LB_SP || c == ROBO_U_LB_ZW;
}

static int robo_lb_hard_break_after(robo_unicode_line_break_class_t c)
{
    return c == ROBO_U_LB_BK || c == ROBO_U_LB_CR || c == ROBO_U_LB_LF ||
           c == ROBO_U_LB_NL;
}

static int robo_lb_is_east_asian(robo_uchar32_t cp)
{
    robo_unicode_east_asian_width_t w = robo_unicode_east_asian_width(cp);
    return w == ROBO_U_EAW_FULLWIDTH || w == ROBO_U_EAW_WIDE ||
           w == ROBO_U_EAW_HALFWIDTH;
}

static int robo_lb_is_qu_pi(robo_uchar32_t cp)
{
    return robo_unicode_line_break_class(cp) == ROBO_U_LB_QU &&
           robo_unicode_category(cp) == ROBO_U_GC_PI;
}

static int robo_lb_is_qu_pf(robo_uchar32_t cp)
{
    return robo_unicode_line_break_class(cp) == ROBO_U_LB_QU &&
           robo_unicode_category(cp) == ROBO_U_GC_PF;
}

static int robo_lb_is_ext_pict_cn(robo_uchar32_t cp)
{
    return robo_unicode_is_extended_pictographic(cp) &&
           robo_unicode_category(cp) == ROBO_U_GC_CN;
}

/* Skip trailing CM/ZWJ from j; return base index or (size_t)-1. */
static size_t robo_lb_skip_cm_back(const robo_unicode_line_break_class_t *cls,
                                   size_t j)
{
    while (j != (size_t)-1 && robo_lb_is_cm(cls[j])) {
        if (j == 0u) {
            return (size_t)-1;
        }
        j--;
    }
    return j;
}

/*
 * Effective class before break position i (LB9/LB10).
 * Returns 0 if sot; else 1 and sets *out_c / *out_i.
 */
static int robo_lb_left_base(const robo_unicode_line_break_class_t *cls,
                             size_t i,
                             robo_unicode_line_break_class_t *out_c,
                             size_t *out_i)
{
    size_t j;
    size_t k;

    if (i == 0u) {
        return 0;
    }
    j = i - 1u;
    if (robo_lb_is_cm(cls[j])) {
        k = robo_lb_skip_cm_back(cls, j);
        if (k != (size_t)-1 && !robo_lb_no_break_cm_base(cls[k])) {
            *out_c = cls[k];
            *out_i = k;
            return 1;
        }
        *out_c = ROBO_U_LB_AL; /* LB10 orphan */
        *out_i = j;
        return 1;
    }
    *out_c = cls[j];
    *out_i = j;
    return 1;
}

static int robo_lb_right_eff(const robo_unicode_line_break_class_t *cls,
                             size_t len,
                             size_t i,
                             robo_unicode_line_break_class_t *out_c)
{
    if (i >= len) {
        return 0;
    }
    if (robo_lb_is_cm(cls[i])) {
        *out_c = ROBO_U_LB_AL; /* LB10 */
        return 1;
    }
    *out_c = cls[i];
    return 1;
}

static int robo_lb_match_zw_sp_star_div(const robo_unicode_line_break_class_t *cls,
                                        size_t i)
{
    size_t j;

    if (i == 0u) {
        return 0;
    }
    j = i - 1u;
    while (j != (size_t)-1 && cls[j] == ROBO_U_LB_SP) {
        if (j == 0u) {
            return 0;
        }
        j--;
    }
    return j != (size_t)-1 && cls[j] == ROBO_U_LB_ZW;
}

static int robo_lb_op_sp_star(const robo_unicode_line_break_class_t *cls, size_t i)
{
    size_t j;
    robo_unicode_line_break_class_t c = ROBO_U_LB_XX;
    size_t bi = 0;

    if (i == 0u) {
        return 0;
    }
    j = i - 1u;
    while (j != (size_t)-1 && cls[j] == ROBO_U_LB_SP) {
        if (j == 0u) {
            return 0;
        }
        j--;
    }
    if (j == (size_t)-1) {
        return 0;
    }
    if (!robo_lb_left_base(cls, j + 1u, &c, &bi)) {
        return 0;
    }
    return c == ROBO_U_LB_OP;
}

static int robo_lb_cl_cp_sp_star(const robo_unicode_line_break_class_t *cls,
                                 size_t i)
{
    size_t j;
    robo_unicode_line_break_class_t c = ROBO_U_LB_XX;
    size_t bi = 0;

    if (i == 0u) {
        return 0;
    }
    j = i - 1u;
    while (j != (size_t)-1 && cls[j] == ROBO_U_LB_SP) {
        if (j == 0u) {
            return 0;
        }
        j--;
    }
    if (j == (size_t)-1) {
        return 0;
    }
    if (!robo_lb_left_base(cls, j + 1u, &c, &bi)) {
        return 0;
    }
    return c == ROBO_U_LB_CL || c == ROBO_U_LB_CP;
}

static int robo_lb_b2_sp_star(const robo_unicode_line_break_class_t *cls, size_t i)
{
    size_t j;
    robo_unicode_line_break_class_t c = ROBO_U_LB_XX;
    size_t bi = 0;

    if (i == 0u) {
        return 0;
    }
    j = i - 1u;
    while (j != (size_t)-1 && cls[j] == ROBO_U_LB_SP) {
        if (j == 0u) {
            return 0;
        }
        j--;
    }
    if (j == (size_t)-1) {
        return 0;
    }
    if (!robo_lb_left_base(cls, j + 1u, &c, &bi)) {
        return 0;
    }
    return c == ROBO_U_LB_B2;
}

static int robo_lb15a(const robo_unicode_line_break_class_t *cls,
                      const robo_uchar32_t *cps,
                      size_t i)
{
    size_t j;
    size_t qu_idx;
    robo_unicode_line_break_class_t before_c = ROBO_U_LB_XX;
    size_t bi = 0;

    if (i == 0u) {
        return 0;
    }
    j = i - 1u;
    while (j != (size_t)-1 && cls[j] == ROBO_U_LB_SP) {
        if (j == 0u) {
            return 0;
        }
        j--;
    }
    if (j == (size_t)-1) {
        return 0;
    }
    if (robo_lb_is_cm(cls[j])) {
        size_t k = robo_lb_skip_cm_back(cls, j);
        if (k == (size_t)-1 || robo_lb_no_break_cm_base(cls[k])) {
            return 0;
        }
        qu_idx = k;
    } else {
        qu_idx = j;
    }
    if (!robo_lb_is_qu_pi(cps[qu_idx])) {
        return 0;
    }
    if (!robo_lb_left_base(cls, qu_idx, &before_c, &bi)) {
        return 1; /* sot */
    }
    return before_c == ROBO_U_LB_BK || before_c == ROBO_U_LB_CR ||
           before_c == ROBO_U_LB_LF || before_c == ROBO_U_LB_NL ||
           before_c == ROBO_U_LB_OP || before_c == ROBO_U_LB_QU ||
           before_c == ROBO_U_LB_GL || before_c == ROBO_U_LB_SP ||
           before_c == ROBO_U_LB_ZW;
}

static int robo_lb15b(const robo_unicode_line_break_class_t *cls,
                      const robo_uchar32_t *cps,
                      size_t len,
                      size_t i)
{
    size_t k;
    robo_unicode_line_break_class_t fol;

    if (i >= len) {
        return 0;
    }
    if (!robo_lb_is_qu_pf(cps[i])) {
        return 0;
    }
    k = i + 1u;
    while (k < len && robo_lb_is_cm(cls[k])) {
        k++;
    }
    if (k >= len) {
        return 1; /* eot */
    }
    fol = cls[k];
    return fol == ROBO_U_LB_SP || fol == ROBO_U_LB_GL || fol == ROBO_U_LB_WJ ||
           fol == ROBO_U_LB_CL || fol == ROBO_U_LB_QU || fol == ROBO_U_LB_CP ||
           fol == ROBO_U_LB_EX || fol == ROBO_U_LB_IS || fol == ROBO_U_LB_SY ||
           fol == ROBO_U_LB_BK || fol == ROBO_U_LB_CR || fol == ROBO_U_LB_LF ||
           fol == ROBO_U_LB_NL || fol == ROBO_U_LB_ZW;
}

static int robo_lb_ri_odd_pair(const robo_unicode_line_break_class_t *cls,
                               size_t i)
{
    size_t count = 0;
    size_t j;

    if (i == 0u) {
        return 0;
    }
    j = i - 1u;
    while (j != (size_t)-1) {
        if (robo_lb_is_cm(cls[j])) {
            size_t k = robo_lb_skip_cm_back(cls, j);
            if (k == (size_t)-1 || robo_lb_no_break_cm_base(cls[k])) {
                break;
            }
            if (cls[k] != ROBO_U_LB_RI) {
                break;
            }
            count++;
            if (k == 0u) {
                break;
            }
            j = k - 1u;
            continue;
        }
        if (cls[j] != ROBO_U_LB_RI) {
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

static int robo_lb_nu_sy_is_star_before(const robo_unicode_line_break_class_t *cls,
                                        size_t i)
{
    size_t j;

    if (i == 0u) {
        return 0;
    }
    j = i - 1u;
    while (j != (size_t)-1) {
        if (robo_lb_is_cm(cls[j])) {
            size_t k = robo_lb_skip_cm_back(cls, j);
            if (k == (size_t)-1 || robo_lb_no_break_cm_base(cls[k])) {
                return 0;
            }
            if (cls[k] == ROBO_U_LB_SY || cls[k] == ROBO_U_LB_IS) {
                if (k == 0u) {
                    return 0;
                }
                j = k - 1u;
                continue;
            }
            if (cls[k] == ROBO_U_LB_NU) {
                return 1;
            }
            return 0;
        }
        if (cls[j] == ROBO_U_LB_SY || cls[j] == ROBO_U_LB_IS) {
            if (j == 0u) {
                return 0;
            }
            j--;
            continue;
        }
        if (cls[j] == ROBO_U_LB_NU) {
            return 1;
        }
        return 0;
    }
    return 0;
}

static int robo_lb_nu_sy_is_star_cl_cp_before(
    const robo_unicode_line_break_class_t *cls,
    size_t i,
    robo_unicode_line_break_class_t which)
{
    size_t j;

    if (i == 0u) {
        return 0;
    }
    j = i - 1u;
    if (robo_lb_is_cm(cls[j])) {
        size_t k = robo_lb_skip_cm_back(cls, j);
        if (k == (size_t)-1 || robo_lb_no_break_cm_base(cls[k])) {
            return 0;
        }
        if (cls[k] != which) {
            return 0;
        }
        return robo_lb_nu_sy_is_star_before(cls, k);
    }
    if (cls[j] != which) {
        return 0;
    }
    return robo_lb_nu_sy_is_star_before(cls, j);
}

static int robo_lb20a(const robo_unicode_line_break_class_t *cls,
                      const robo_uchar32_t *cps,
                      size_t len,
                      size_t i)
{
    robo_unicode_line_break_class_t right = ROBO_U_LB_XX;
    robo_unicode_line_break_class_t left_c = ROBO_U_LB_XX;
    robo_unicode_line_break_class_t before_c = ROBO_U_LB_XX;
    size_t left_i = 0;
    size_t bi = 0;

    if (!robo_lb_right_eff(cls, len, i, &right) || right != ROBO_U_LB_AL) {
        return 0;
    }
    if (!robo_lb_left_base(cls, i, &left_c, &left_i)) {
        return 0;
    }
    if (left_c == ROBO_U_LB_HY) {
        /* ok */
    } else if (left_c == ROBO_U_LB_BA && cps[left_i] == ROBO_LB_HYPHEN_CP) {
        /* ok */
    } else {
        return 0;
    }
    if (!robo_lb_left_base(cls, left_i, &before_c, &bi)) {
        return 1;
    }
    return before_c == ROBO_U_LB_BK || before_c == ROBO_U_LB_CR ||
           before_c == ROBO_U_LB_LF || before_c == ROBO_U_LB_NL ||
           before_c == ROBO_U_LB_SP || before_c == ROBO_U_LB_ZW ||
           before_c == ROBO_U_LB_CB || before_c == ROBO_U_LB_GL;
}

static int robo_lb_is_ak_as_or_dotted(const robo_unicode_line_break_class_t *cls,
                                      const robo_uchar32_t *cps,
                                      size_t len,
                                      size_t idx,
                                      robo_unicode_line_break_class_t c)
{
    if (c == ROBO_U_LB_AK || c == ROBO_U_LB_AS) {
        return 1;
    }
    return idx < len && !robo_lb_is_cm(cls[idx]) &&
           cps[idx] == ROBO_LB_DOTTED_CIRCLE;
}

static int robo_lb28a(const robo_unicode_line_break_class_t *cls,
                      const robo_uchar32_t *cps,
                      size_t len,
                      size_t i)
{
    robo_unicode_line_break_class_t left = ROBO_U_LB_XX;
    robo_unicode_line_break_class_t right = ROBO_U_LB_XX;
    robo_unicode_line_break_class_t before = ROBO_U_LB_XX;
    size_t li = 0;
    size_t bi = 0;
    size_t k;

    if (!robo_lb_left_base(cls, i, &left, &li)) {
        return 0;
    }
    if (!robo_lb_right_eff(cls, len, i, &right)) {
        return 0;
    }

    if (left == ROBO_U_LB_AP &&
        robo_lb_is_ak_as_or_dotted(cls, cps, len, i, right)) {
        return 1;
    }

    if (robo_lb_is_ak_as_or_dotted(cls, cps, len, li, left) &&
        (right == ROBO_U_LB_VF || right == ROBO_U_LB_VI)) {
        return 1;
    }

    if (left == ROBO_U_LB_VI) {
        if (robo_lb_left_base(cls, li, &before, &bi) &&
            robo_lb_is_ak_as_or_dotted(cls, cps, len, bi, before)) {
            if (right == ROBO_U_LB_AK ||
                (i < len && !robo_lb_is_cm(cls[i]) &&
                 cps[i] == ROBO_LB_DOTTED_CIRCLE)) {
                return 1;
            }
        }
    }

    if (robo_lb_is_ak_as_or_dotted(cls, cps, len, li, left) &&
        robo_lb_is_ak_as_or_dotted(cls, cps, len, i, right)) {
        k = i + 1u;
        while (k < len && robo_lb_is_cm(cls[k])) {
            k++;
        }
        if (k < len && cls[k] == ROBO_U_LB_VF) {
            return 1;
        }
    }

    return 0;
}

static int robo_lb25(const robo_unicode_line_break_class_t *cls,
                     size_t len,
                     size_t i,
                     robo_unicode_line_break_class_t left,
                     robo_unicode_line_break_class_t right)
{
    size_t k;
    size_t k2;

    if (right == ROBO_U_LB_PO || right == ROBO_U_LB_PR || right == ROBO_U_LB_NU) {
        if (robo_lb_nu_sy_is_star_cl_cp_before(cls, i, ROBO_U_LB_CL) &&
            (right == ROBO_U_LB_PO || right == ROBO_U_LB_PR)) {
            return 1;
        }
        if (robo_lb_nu_sy_is_star_cl_cp_before(cls, i, ROBO_U_LB_CP) &&
            (right == ROBO_U_LB_PO || right == ROBO_U_LB_PR)) {
            return 1;
        }
        if (robo_lb_nu_sy_is_star_before(cls, i) &&
            (right == ROBO_U_LB_PO || right == ROBO_U_LB_PR ||
             right == ROBO_U_LB_NU)) {
            return 1;
        }
    }

    if (left == ROBO_U_LB_PO) {
        if (right == ROBO_U_LB_NU) {
            return 1;
        }
        if (right == ROBO_U_LB_OP) {
            k = i + 1u;
            while (k < len && robo_lb_is_cm(cls[k])) {
                k++;
            }
            if (k < len && cls[k] == ROBO_U_LB_NU) {
                return 1;
            }
            if (k < len && cls[k] == ROBO_U_LB_IS) {
                k2 = k + 1u;
                while (k2 < len && robo_lb_is_cm(cls[k2])) {
                    k2++;
                }
                if (k2 < len && cls[k2] == ROBO_U_LB_NU) {
                    return 1;
                }
            }
        }
    }

    if (left == ROBO_U_LB_PR) {
        if (right == ROBO_U_LB_NU) {
            return 1;
        }
        if (right == ROBO_U_LB_OP) {
            k = i + 1u;
            while (k < len && robo_lb_is_cm(cls[k])) {
                k++;
            }
            if (k < len && cls[k] == ROBO_U_LB_NU) {
                return 1;
            }
            if (k < len && cls[k] == ROBO_U_LB_IS) {
                k2 = k + 1u;
                while (k2 < len && robo_lb_is_cm(cls[k2])) {
                    k2++;
                }
                if (k2 < len && cls[k2] == ROBO_U_LB_NU) {
                    return 1;
                }
            }
        }
    }

    if (left == ROBO_U_LB_HY && right == ROBO_U_LB_NU) {
        return 1;
    }
    if (left == ROBO_U_LB_IS && right == ROBO_U_LB_NU) {
        return 1;
    }
    return 0;
}

static robo_unicode_line_break_op_t robo_lb_opportunity_with_cls(
    const robo_uchar32_t *cps,
    const robo_unicode_line_break_class_t *cls,
    size_t len,
    size_t index)
{
    robo_unicode_line_break_class_t left_raw;
    robo_unicode_line_break_class_t right_raw;
    robo_unicode_line_break_class_t left = ROBO_U_LB_XX;
    robo_unicode_line_break_class_t right = ROBO_U_LB_XX;
    robo_unicode_line_break_class_t before = ROBO_U_LB_XX;
    size_t left_i = 0;
    size_t bi = 0;
    size_t k;

    if (index == 0u) {
        return ROBO_LINE_BREAK_PROHIBITED; /* LB2 */
    }
    if (index >= len) {
        return ROBO_LINE_BREAK_MANDATORY; /* LB3 */
    }

    left_raw = cls[index - 1u];
    right_raw = cls[index];

    if (left_raw == ROBO_U_LB_BK) {
        return ROBO_LINE_BREAK_MANDATORY;
    }
    if (left_raw == ROBO_U_LB_CR && right_raw == ROBO_U_LB_LF) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }
    if (left_raw == ROBO_U_LB_CR || left_raw == ROBO_U_LB_LF ||
        left_raw == ROBO_U_LB_NL) {
        return ROBO_LINE_BREAK_MANDATORY;
    }
    if (robo_lb_hard_break_after(right_raw)) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }
    if (right_raw == ROBO_U_LB_SP || right_raw == ROBO_U_LB_ZW) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }
    if (robo_lb_match_zw_sp_star_div(cls, index)) {
        return ROBO_LINE_BREAK_ALLOWED;
    }
    if (left_raw == ROBO_U_LB_ZWJ) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }
    if (robo_lb_is_cm(right_raw) && !robo_lb_no_break_cm_base(left_raw)) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }

    (void)robo_lb_left_base(cls, index, &left, &left_i);
    (void)robo_lb_right_eff(cls, len, index, &right);

    if (right == ROBO_U_LB_WJ || left == ROBO_U_LB_WJ) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }
    if (left == ROBO_U_LB_GL) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }
    if (right == ROBO_U_LB_GL && left != ROBO_U_LB_SP && left != ROBO_U_LB_BA &&
        left != ROBO_U_LB_HY) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }
    if (right == ROBO_U_LB_CL || right == ROBO_U_LB_CP || right == ROBO_U_LB_EX ||
        right == ROBO_U_LB_SY) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }
    if (robo_lb_op_sp_star(cls, index)) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }
    if (robo_lb15a(cls, cps, index)) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }
    if (robo_lb15b(cls, cps, len, index)) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }
    if (left_raw == ROBO_U_LB_SP && right == ROBO_U_LB_IS) {
        k = index + 1u;
        while (k < len && robo_lb_is_cm(cls[k])) {
            k++;
        }
        if (k < len && cls[k] == ROBO_U_LB_NU) {
            return ROBO_LINE_BREAK_ALLOWED;
        }
    }
    if (right == ROBO_U_LB_IS) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }
    if (right == ROBO_U_LB_NS && robo_lb_cl_cp_sp_star(cls, index)) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }
    if (right == ROBO_U_LB_B2 && robo_lb_b2_sp_star(cls, index)) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }
    if (left_raw == ROBO_U_LB_SP) {
        return ROBO_LINE_BREAK_ALLOWED;
    }

    if (right == ROBO_U_LB_QU && !robo_lb_is_qu_pi(cps[index])) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }
    if (left == ROBO_U_LB_QU && !robo_lb_is_qu_pf(cps[left_i])) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }

    if (right == ROBO_U_LB_QU && !robo_lb_is_east_asian(cps[left_i])) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }
    if (right == ROBO_U_LB_QU) {
        k = index + 1u;
        while (k < len && robo_lb_is_cm(cls[k])) {
            k++;
        }
        if (k >= len || !robo_lb_is_east_asian(cps[k])) {
            return ROBO_LINE_BREAK_PROHIBITED;
        }
    }
    if (left == ROBO_U_LB_QU) {
        if (robo_lb_is_cm(cls[index]) || !robo_lb_is_east_asian(cps[index])) {
            return ROBO_LINE_BREAK_PROHIBITED;
        }
    }
    if (left == ROBO_U_LB_QU) {
        if (!robo_lb_left_base(cls, left_i, &before, &bi) ||
            !robo_lb_is_east_asian(cps[bi])) {
            return ROBO_LINE_BREAK_PROHIBITED;
        }
    }

    if (right == ROBO_U_LB_CB || left == ROBO_U_LB_CB) {
        return ROBO_LINE_BREAK_ALLOWED;
    }

    if (robo_lb20a(cls, cps, len, index)) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }

    if (right == ROBO_U_LB_BA || right == ROBO_U_LB_HY || right == ROBO_U_LB_NS) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }
    if (left == ROBO_U_LB_BB) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }

    if ((left == ROBO_U_LB_HY || left == ROBO_U_LB_BA) && right != ROBO_U_LB_HL) {
        if (!(left == ROBO_U_LB_BA && robo_lb_is_east_asian(cps[left_i]))) {
            if (robo_lb_left_base(cls, left_i, &before, &bi) &&
                before == ROBO_U_LB_HL) {
                return ROBO_LINE_BREAK_PROHIBITED;
            }
        }
    }

    if (left == ROBO_U_LB_SY && right == ROBO_U_LB_HL) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }

    if (right == ROBO_U_LB_IN) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }

    if ((left == ROBO_U_LB_AL || left == ROBO_U_LB_HL) && right == ROBO_U_LB_NU) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }
    if (left == ROBO_U_LB_NU &&
        (right == ROBO_U_LB_AL || right == ROBO_U_LB_HL)) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }

    if (left == ROBO_U_LB_PR &&
        (right == ROBO_U_LB_ID || right == ROBO_U_LB_EB ||
         right == ROBO_U_LB_EM)) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }
    if ((left == ROBO_U_LB_ID || left == ROBO_U_LB_EB || left == ROBO_U_LB_EM) &&
        right == ROBO_U_LB_PO) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }

    if ((left == ROBO_U_LB_PR || left == ROBO_U_LB_PO) &&
        (right == ROBO_U_LB_AL || right == ROBO_U_LB_HL)) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }
    if ((left == ROBO_U_LB_AL || left == ROBO_U_LB_HL) &&
        (right == ROBO_U_LB_PR || right == ROBO_U_LB_PO)) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }

    if (robo_lb25(cls, len, index, left, right)) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }

    if (left == ROBO_U_LB_JL &&
        (right == ROBO_U_LB_JL || right == ROBO_U_LB_JV ||
         right == ROBO_U_LB_H2 || right == ROBO_U_LB_H3)) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }
    if ((left == ROBO_U_LB_JV || left == ROBO_U_LB_H2) &&
        (right == ROBO_U_LB_JV || right == ROBO_U_LB_JT)) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }
    if ((left == ROBO_U_LB_JT || left == ROBO_U_LB_H3) &&
        right == ROBO_U_LB_JT) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }

    if ((left == ROBO_U_LB_JL || left == ROBO_U_LB_JV || left == ROBO_U_LB_JT ||
         left == ROBO_U_LB_H2 || left == ROBO_U_LB_H3) &&
        right == ROBO_U_LB_PO) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }
    if (left == ROBO_U_LB_PR &&
        (right == ROBO_U_LB_JL || right == ROBO_U_LB_JV ||
         right == ROBO_U_LB_JT || right == ROBO_U_LB_H2 ||
         right == ROBO_U_LB_H3)) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }

    if ((left == ROBO_U_LB_AL || left == ROBO_U_LB_HL) &&
        (right == ROBO_U_LB_AL || right == ROBO_U_LB_HL)) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }

    if (robo_lb28a(cls, cps, len, index)) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }

    if (left == ROBO_U_LB_IS &&
        (right == ROBO_U_LB_AL || right == ROBO_U_LB_HL)) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }

    if ((left == ROBO_U_LB_AL || left == ROBO_U_LB_HL || left == ROBO_U_LB_NU) &&
        right == ROBO_U_LB_OP && !robo_lb_is_east_asian(cps[index])) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }
    if (left == ROBO_U_LB_CP && !robo_lb_is_east_asian(cps[left_i]) &&
        (right == ROBO_U_LB_AL || right == ROBO_U_LB_HL ||
         right == ROBO_U_LB_NU)) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }

    if (left == ROBO_U_LB_RI && right == ROBO_U_LB_RI &&
        robo_lb_ri_odd_pair(cls, index)) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }

    if (left == ROBO_U_LB_EB && right == ROBO_U_LB_EM) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }
    if (right == ROBO_U_LB_EM && robo_lb_is_ext_pict_cn(cps[left_i])) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }

    return ROBO_LINE_BREAK_ALLOWED;
}

robo_unicode_line_break_op_t robo_unicode_line_break_opportunity(
    const robo_uchar32_t *cps, size_t len, size_t index)
{
    robo_unicode_line_break_class_t stack_cls[ROBO_LB_CLS_STACK];
    robo_unicode_line_break_class_t *cls;
    robo_unicode_line_break_op_t op;
    size_t i;
    int heap = 0;

    if (index == 0u) {
        return ROBO_LINE_BREAK_PROHIBITED;
    }
    if (index >= len || !cps) {
        return ROBO_LINE_BREAK_MANDATORY;
    }

    if (len <= ROBO_LB_CLS_STACK) {
        cls = stack_cls;
    } else {
        cls = (robo_unicode_line_break_class_t *)robo_malloc(
            len * sizeof(robo_unicode_line_break_class_t));
        if (!cls) {
            return ROBO_LINE_BREAK_ALLOWED;
        }
        heap = 1;
    }
    for (i = 0; i < len; i++) {
        cls[i] = robo_unicode_line_break_class(cps[i]);
    }
    op = robo_lb_opportunity_with_cls(cps, cls, len, index);
    if (heap) {
        robo_free(cls);
    }
    return op;
}

int robo_unicode_line_break(const robo_uchar32_t *cps, size_t len, size_t index)
{
    return robo_unicode_line_break_opportunity(cps, len, index) !=
           ROBO_LINE_BREAK_PROHIBITED;
}

static robo_result_t robo_line_decode_at(const char *src,
                                         size_t src_len,
                                         size_t offset,
                                         robo_unicode_mode_t mode,
                                         robo_uchar32_t *cp,
                                         size_t *byte_end)
{
    size_t next = offset;
    robo_unicode_status_t st =
        robo_unicode_utf8_decode(src, src_len, offset, mode, cp, &next);
    if (st != ROBO_UNICODE_OK) {
        return ROBO_PARSE_ERROR;
    }
    *byte_end = next;
    return ROBO_OK;
}

static robo_result_t robo_line_next_segment(const char *src,
                                            size_t src_len,
                                            size_t byte_offset,
                                            robo_unicode_mode_t mode,
                                            size_t *line_start,
                                            size_t *line_end,
                                            robo_unicode_line_break_op_t *op)
{
    robo_uchar32_t stack_cps[96];
    size_t stack_ends[96];
    robo_uchar32_t *cps = stack_cps;
    size_t *cp_ends = stack_ends;
    size_t cap = 96u;
    size_t n = 0;
    size_t off = byte_offset;
    robo_unicode_line_break_op_t last_op = ROBO_LINE_BREAK_MANDATORY;
    robo_result_t r;

    if (byte_offset >= src_len) {
        *line_start = src_len;
        *line_end = src_len;
        if (op) {
            *op = ROBO_LINE_BREAK_PROHIBITED;
        }
        return ROBO_OK;
    }
    *line_start = byte_offset;

    for (;;) {
        robo_uchar32_t cp = 0;
        size_t bend = off;
        size_t peek_n;
        size_t peek_off;
        size_t i;

        if (off >= src_len) {
            last_op = ROBO_LINE_BREAK_MANDATORY;
            break;
        }
        r = robo_line_decode_at(src, src_len, off, mode, &cp, &bend);
        if (r != ROBO_OK) {
            if (cps != stack_cps) {
                robo_free(cps);
            }
            if (cp_ends != stack_ends) {
                robo_free(cp_ends);
            }
            return r;
        }

        if (n > 0u) {
            robo_uchar32_t peek_stack[160];
            robo_uchar32_t *peek = peek_stack;
            size_t peek_cap = 160u;
            int heap_peek = 0;
            robo_unicode_line_break_op_t kind;

            if (n + 1u + ROBO_LB_LOOKAHEAD > peek_cap) {
                peek_cap = n + 1u + ROBO_LB_LOOKAHEAD + 32u;
                peek = (robo_uchar32_t *)robo_malloc(peek_cap *
                                                     sizeof(robo_uchar32_t));
                if (!peek) {
                    if (cps != stack_cps) {
                        robo_free(cps);
                    }
                    if (cp_ends != stack_ends) {
                        robo_free(cp_ends);
                    }
                    return ROBO_OUT_OF_MEMORY;
                }
                heap_peek = 1;
            }
            for (i = 0; i < n; i++) {
                peek[i] = cps[i];
            }
            peek[n] = cp;
            peek_n = n + 1u;
            peek_off = bend;
            while (peek_n < n + 1u + ROBO_LB_LOOKAHEAD && peek_off < src_len) {
                robo_uchar32_t pcp = 0;
                size_t pend = peek_off;
                r = robo_line_decode_at(src, src_len, peek_off, mode, &pcp, &pend);
                if (r != ROBO_OK) {
                    if (heap_peek) {
                        robo_free(peek);
                    }
                    if (cps != stack_cps) {
                        robo_free(cps);
                    }
                    if (cp_ends != stack_ends) {
                        robo_free(cp_ends);
                    }
                    return r;
                }
                if (peek_n >= peek_cap) {
                    break;
                }
                peek[peek_n++] = pcp;
                peek_off = pend;
            }
            kind = robo_unicode_line_break_opportunity(peek, peek_n, n);
            if (kind != ROBO_LINE_BREAK_PROHIBITED) {
                last_op = kind;
                if (heap_peek) {
                    robo_free(peek);
                }
                break;
            }
            if (heap_peek) {
                robo_free(peek);
            }
        }

        if (n >= cap) {
            size_t ncap = cap * 2u;
            robo_uchar32_t *nc =
                (robo_uchar32_t *)robo_malloc(ncap * sizeof(robo_uchar32_t));
            size_t *ne = (size_t *)robo_malloc(ncap * sizeof(size_t));
            if (!nc || !ne) {
                robo_free(nc);
                robo_free(ne);
                if (cps != stack_cps) {
                    robo_free(cps);
                }
                if (cp_ends != stack_ends) {
                    robo_free(cp_ends);
                }
                return ROBO_OUT_OF_MEMORY;
            }
            memcpy(nc, cps, n * sizeof(robo_uchar32_t));
            memcpy(ne, cp_ends, n * sizeof(size_t));
            if (cps != stack_cps) {
                robo_free(cps);
            }
            if (cp_ends != stack_ends) {
                robo_free(cp_ends);
            }
            cps = nc;
            cp_ends = ne;
            cap = ncap;
        }
        cps[n] = cp;
        cp_ends[n] = bend;
        n++;
        off = bend;
    }

    *line_end = (n == 0u) ? byte_offset : cp_ends[n - 1u];
    if (op) {
        *op = last_op;
    }
    if (cps != stack_cps) {
        robo_free(cps);
    }
    if (cp_ends != stack_ends) {
        robo_free(cp_ends);
    }
    return ROBO_OK;
}

robo_result_t robo_unicode_line_iter_init(robo_unicode_line_iter_t *it,
                                          const char *src,
                                          size_t src_len,
                                          robo_unicode_mode_t mode)
{
    if (!it) {
        return ROBO_INVALID_ARGUMENT;
    }
    it->src = src;
    it->src_len = src_len;
    it->mode = mode;
    it->byte_offset = 0;
    it->line_index = 0;
    it->valid = 1;
    return ROBO_OK;
}

robo_result_t robo_unicode_line_iter_next(robo_unicode_line_iter_t *it,
                                          size_t *line_start,
                                          size_t *line_end,
                                          robo_unicode_line_break_op_t *op)
{
    robo_result_t r;
    robo_unicode_line_break_op_t kind = ROBO_LINE_BREAK_PROHIBITED;

    if (!it || !line_start || !line_end) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!it->valid) {
        return ROBO_PARSE_ERROR;
    }
    r = robo_line_next_segment(it->src,
                               it->src_len,
                               it->byte_offset,
                               it->mode,
                               line_start,
                               line_end,
                               &kind);
    if (r != ROBO_OK) {
        it->valid = 0;
        return r;
    }
    if (op) {
        *op = kind;
    }
    it->byte_offset = *line_end;
    if (*line_start < it->src_len && *line_start != *line_end) {
        it->line_index++;
    }
    return ROBO_OK;
}

robo_result_t robo_unicode_line_break_next(const char *src,
                                           size_t src_len,
                                           size_t byte_offset,
                                           robo_unicode_mode_t mode,
                                           size_t *line_start,
                                           size_t *line_end,
                                           robo_unicode_line_break_op_t *op)
{
    if (byte_offset > src_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    return robo_line_next_segment(
        src, src_len, byte_offset, mode, line_start, line_end, op);
}

robo_result_t robo_unicode_line_break_prev(const char *src,
                                           size_t src_len,
                                           size_t byte_offset,
                                           robo_unicode_mode_t mode,
                                           size_t *line_start,
                                           size_t *line_end,
                                           robo_unicode_line_break_op_t *op)
{
    robo_unicode_line_iter_t it;
    size_t ls = 0;
    size_t le = 0;
    size_t last_ls = 0;
    size_t last_le = 0;
    robo_unicode_line_break_op_t kind = ROBO_LINE_BREAK_PROHIBITED;
    robo_unicode_line_break_op_t last_kind = ROBO_LINE_BREAK_PROHIBITED;

    if (!line_start || !line_end) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (byte_offset > src_len) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (byte_offset == 0u) {
        *line_start = 0;
        *line_end = 0;
        if (op) {
            *op = ROBO_LINE_BREAK_PROHIBITED;
        }
        return ROBO_OK;
    }

    robo_unicode_line_iter_init(&it, src, src_len, mode);
    while (it.byte_offset < byte_offset && it.valid) {
        last_ls = ls;
        last_le = le;
        last_kind = kind;
        if (robo_unicode_line_iter_next(&it, &ls, &le, &kind) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        if (le >= byte_offset) {
            if (ls < byte_offset) {
                *line_start = ls;
                *line_end = le;
                if (op) {
                    *op = kind;
                }
            } else {
                *line_start = last_ls;
                *line_end = last_le;
                if (op) {
                    *op = last_kind;
                }
            }
            return ROBO_OK;
        }
    }
    *line_start = last_ls;
    *line_end = last_le;
    if (op) {
        *op = last_kind;
    }
    return ROBO_OK;
}

robo_result_t robo_unicode_line_break_count_utf8(const char *src,
                                                 size_t src_len,
                                                 robo_unicode_mode_t mode,
                                                 size_t *count)
{
    robo_unicode_line_iter_t it;
    size_t ls = 0;
    size_t le = 0;
    size_t n = 0;
    robo_unicode_line_break_op_t kind = ROBO_LINE_BREAK_PROHIBITED;
    robo_result_t r;

    if (!count) {
        return ROBO_INVALID_ARGUMENT;
    }
    *count = 0;
    if (src_len == 0u) {
        return ROBO_OK;
    }
    if (!src) {
        return ROBO_INVALID_ARGUMENT;
    }

    robo_unicode_line_iter_init(&it, src, src_len, mode);
    while (it.byte_offset < src_len && it.valid) {
        r = robo_unicode_line_iter_next(&it, &ls, &le, &kind);
        if (r != ROBO_OK) {
            return r;
        }
        if (ls >= src_len && le >= src_len) {
            break;
        }
        n++;
    }
    *count = n;
    return ROBO_OK;
}
