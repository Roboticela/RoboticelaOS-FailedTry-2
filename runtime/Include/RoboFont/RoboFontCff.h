/*
 * RoboFont CFF / CFF2 (Goal 5.5).
 *
 * Parses and validates Compact Font Format tables inside SFNT/OTTO fonts.
 * Does NOT produce final glyph outlines (Goal 5.8), evaluate variable-font
 * instances (5.18), map Unicode via cmap (5.6), or rasterize/hint.
 *
 * Outline technology (TrueType / CFF / CFF2) remains classified by SFNT tags;
 * this module parses the CFF/CFF2 table bodies.
 */

#ifndef ROBO_FONT_CFF_H
#define ROBO_FONT_CFF_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontBinary.h"
#include "RoboFont/RoboFontOt.h"
#include "RoboFont/RoboFontSfnt.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Resource limits ---- */

#define ROBO_FONT_CFF_MAX_INDEX_COUNT 65536u
#define ROBO_FONT_CFF_MAX_INDEX_OBJECT (1u << 20) /* 1 MiB per object */
#define ROBO_FONT_CFF_MAX_DICT_OPS 512u
#define ROBO_FONT_CFF_MAX_DICT_OPERANDS 48u
#define ROBO_FONT_CFF_MAX_CHARSTRING_LEN (1u << 16)
#define ROBO_FONT_CFF_MAX_OPERAND_STACK 48u
#define ROBO_FONT_CFF_MAX_SUBR_DEPTH 16u
#define ROBO_FONT_CFF_MAX_SUBR_CALLS 1024u
#define ROBO_FONT_CFF_MAX_FDARRAY 256u
#define ROBO_FONT_CFF_MAX_VAR_REGIONS 4096u
#define ROBO_FONT_CFF_MAX_CS_OPS 8192u
#define ROBO_FONT_CFF_MAX_GLYPHS 65535u
#define ROBO_FONT_CFF_MAX_STRING_INDEX 65000u

/* ---- Variable-size offset ---- */

robo_result_t robo_font_cff_read_offset(robo_font_reader_t *r,
                                        uint8_t off_size,
                                        uint32_t *out);

/* ---- INDEX ---- */

typedef struct robo_font_cff_index {
    uint32_t count;
    uint8_t off_size;
    /** Absolute offsets into @p table for each object (count entries). */
    uint32_t *data_offsets; /* length count; relative to table base */
    uint32_t *data_lengths; /* length count */
    robo_font_span_t table; /* owning table view (borrowed) */
    size_t data_base;       /* absolute offset of first data byte */
} robo_font_cff_index_t;

void robo_font_cff_index_clear(robo_font_cff_index_t *idx);

/**
 * Parse a CFF1 INDEX (Card16 count) starting at @p offset within @p table.
 * On success, @p out_end is set to the first byte past the INDEX.
 */
robo_result_t robo_font_cff_index_parse(robo_font_span_t table,
                                        size_t offset,
                                        robo_font_cff_index_t *out,
                                        size_t *out_end);

/** CFF2 INDEX uses Card32 count. */
robo_result_t robo_font_cff2_index_parse(robo_font_span_t table,
                                         size_t offset,
                                         robo_font_cff_index_t *out,
                                         size_t *out_end);

uint32_t robo_font_cff_index_count(const robo_font_cff_index_t *idx);

robo_result_t robo_font_cff_index_get(const robo_font_cff_index_t *idx,
                                      uint32_t i,
                                      robo_font_span_t *out);

robo_result_t robo_font_cff_index_get_offset(const robo_font_cff_index_t *idx,
                                             uint32_t i,
                                             uint32_t *out_abs);

robo_result_t robo_font_cff_index_get_length(const robo_font_cff_index_t *idx,
                                             uint32_t i,
                                             uint32_t *out_len);

/** Standard subroutine bias for @p nSubrs. */
uint32_t robo_font_cff_subr_bias(uint32_t n_subrs);

/* ---- DICT ---- */

typedef enum robo_font_cff_dict_op {
    ROBO_CFF_OP_VERSION = 0,
    ROBO_CFF_OP_NOTICE = 1,
    ROBO_CFF_OP_FULLNAME = 2,
    ROBO_CFF_OP_FAMILYNAME = 3,
    ROBO_CFF_OP_WEIGHT = 4,
    ROBO_CFF_OP_FONTBBOX = 5,
    ROBO_CFF_OP_BLUEVALUES = 6,
    ROBO_CFF_OP_OTHERBLUES = 7,
    ROBO_CFF_OP_FAMILYBLUES = 8,
    ROBO_CFF_OP_FAMILYOTHERBLUES = 9,
    ROBO_CFF_OP_STDHW = 10,
    ROBO_CFF_OP_STDVW = 11,
    ROBO_CFF_OP_ESCAPE = 12,
    ROBO_CFF_OP_UNIQUEID = 13,
    ROBO_CFF_OP_XUID = 14,
    ROBO_CFF_OP_CHARSET = 15,
    ROBO_CFF_OP_ENCODING = 16,
    ROBO_CFF_OP_CHARSTRINGS = 17,
    ROBO_CFF_OP_PRIVATE = 18, /* size, offset */
    ROBO_CFF_OP_SUBRS = 19,
    ROBO_CFF_OP_DEFAULTWIDTHX = 20,
    ROBO_CFF_OP_NOMINALWIDTHX = 21,
    /* escape (12) operators — encoded as 1200 + second byte */
    ROBO_CFF_OP_COPYRIGHT = 1200,
    ROBO_CFF_OP_ISFIXEDPITCH = 1201,
    ROBO_CFF_OP_ITALICANGLE = 1202,
    ROBO_CFF_OP_UNDERLINEPOSITION = 1203,
    ROBO_CFF_OP_UNDERLINETHICKNESS = 1204,
    ROBO_CFF_OP_PAINTTYPE = 1205,
    ROBO_CFF_OP_CHARSTRINGTYPE = 1206,
    ROBO_CFF_OP_FONTMATRIX = 1207,
    ROBO_CFF_OP_STROKEWIDTH = 1208,
    ROBO_CFF_OP_BLUESCALE = 1209,
    ROBO_CFF_OP_BLUESHIFT = 1210,
    ROBO_CFF_OP_BLUEFUZZ = 1211,
    ROBO_CFF_OP_STEMSNAPH = 1212,
    ROBO_CFF_OP_STEMSNAPV = 1213,
    ROBO_CFF_OP_FORCEBOLD = 1214,
    ROBO_CFF_OP_LANGUAGEGROUP = 1217,
    ROBO_CFF_OP_EXPANSIONFACTOR = 1218,
    ROBO_CFF_OP_INITIALRANDOMSEED = 1219,
    ROBO_CFF_OP_SYNTHETICBASE = 1220,
    ROBO_CFF_OP_POSTSCRIPT = 1221,
    ROBO_CFF_OP_BASEFONTNAME = 1222,
    ROBO_CFF_OP_BASEFONTBLEND = 1223,
    ROBO_CFF_OP_ROS = 1230,
    ROBO_CFF_OP_CIDFONTVERSION = 1231,
    ROBO_CFF_OP_CIDFONTREVISION = 1232,
    ROBO_CFF_OP_CIDFONTTYPE = 1233,
    ROBO_CFF_OP_CIDCOUNT = 1234,
    ROBO_CFF_OP_UIDBASE = 1235,
    ROBO_CFF_OP_FDARRAY = 1236,
    ROBO_CFF_OP_FDSELECT = 1237,
    ROBO_CFF_OP_FONTNAME = 1238,
    /* CFF2 escape ops (12,xx). In CFF2 Top DICT, single-byte 15/16 are
     * vsindex/blend (same codes as CFF1 charset/encoding — context differs). */
    ROBO_CFF_OP_VSTORE = 1224,
    ROBO_CFF_OP_MAXSTACK = 1225
} robo_font_cff_dict_op_t;

typedef struct robo_font_cff_number {
    int32_t i;     /* integer (or truncated real) */
    double real;   /* real when is_real */
    int is_real;
} robo_font_cff_number_t;

typedef struct robo_font_cff_dict_entry {
    uint16_t op; /* robo_font_cff_dict_op_t or raw */
    uint8_t argc;
    robo_font_cff_number_t args[ROBO_FONT_CFF_MAX_DICT_OPERANDS];
} robo_font_cff_dict_entry_t;

typedef struct robo_font_cff_dict {
    uint32_t count;
    robo_font_cff_dict_entry_t *entries;
} robo_font_cff_dict_t;

void robo_font_cff_dict_clear(robo_font_cff_dict_t *d);

robo_result_t robo_font_cff_dict_parse(robo_font_span_t dict_bytes,
                                       robo_font_cff_dict_t *out);

/** Find first entry with operator @p op; ROBO_NOT_FOUND if absent. */
robo_result_t robo_font_cff_dict_find(const robo_font_cff_dict_t *d,
                                      uint16_t op,
                                      const robo_font_cff_dict_entry_t **out);

/* ---- Structured Top / Private DICT summaries ---- */

typedef struct robo_font_cff_top_dict {
    robo_font_cff_dict_t raw;
    int32_t charset;      /* offset or predefined 0/1/2; -1 unset */
    int32_t encoding;     /* offset or predefined 0/1; -1 unset */
    int32_t charstrings;  /* offset; -1 unset */
    int32_t private_size;
    int32_t private_offset;
    int32_t fdarray;
    int32_t fdselect;
    int32_t vstore; /* CFF2 */
    int32_t ros_registry;
    int32_t ros_ordering;
    int32_t ros_supplement;
    int is_cid;
    int32_t cid_count;
    double font_matrix[6];
    int has_font_matrix;
    int32_t font_bbox[4];
    int has_font_bbox;
    int32_t charstring_type; /* default 2 */
} robo_font_cff_top_dict_t;

typedef struct robo_font_cff_private_dict {
    robo_font_cff_dict_t raw;
    int32_t subrs; /* offset relative to Private DICT start; -1 unset */
    double default_width_x;
    double nominal_width_x;
    int has_default_width;
    int has_nominal_width;
} robo_font_cff_private_dict_t;

void robo_font_cff_top_dict_clear(robo_font_cff_top_dict_t *t);
void robo_font_cff_private_dict_clear(robo_font_cff_private_dict_t *p);

robo_result_t robo_font_cff_top_dict_interpret(const robo_font_cff_dict_t *raw,
                                               robo_font_cff_top_dict_t *out);

robo_result_t robo_font_cff_private_dict_interpret(const robo_font_cff_dict_t *raw,
                                                   robo_font_cff_private_dict_t *out);

/* ---- Charset / Encoding / FDSelect ---- */

typedef struct robo_font_cff_charset {
    uint8_t format;
    uint32_t glyph_count; /* including .notdef */
    uint16_t *sids;       /* glyph_count SIDs; sids[0]=0 for .notdef */
} robo_font_cff_charset_t;

void robo_font_cff_charset_clear(robo_font_cff_charset_t *c);

robo_result_t robo_font_cff_charset_parse(robo_font_span_t table,
                                          size_t offset,
                                          uint32_t n_glyphs,
                                          int is_cid,
                                          robo_font_cff_charset_t *out);

robo_result_t robo_font_cff_charset_sid(const robo_font_cff_charset_t *c,
                                        uint32_t gid,
                                        uint16_t *out_sid);

typedef struct robo_font_cff_encoding {
    uint8_t format; /* 0/1 or 0x80|format with supplement */
    uint32_t n_codes;
    uint8_t *codes; /* code for glyphs 1..n (format 0) or expanded */
    uint32_t n_supplements;
    uint8_t *supp_codes;
    uint16_t *supp_sids;
    int is_predefined; /* 0 StandardEncoding, 1 ExpertEncoding */
} robo_font_cff_encoding_t;

void robo_font_cff_encoding_clear(robo_font_cff_encoding_t *e);

robo_result_t robo_font_cff_encoding_parse(robo_font_span_t table,
                                           size_t offset,
                                           uint32_t n_glyphs,
                                           robo_font_cff_encoding_t *out);

typedef struct robo_font_cff_fdselect {
    uint8_t format;
    uint32_t n_glyphs;
    uint8_t *fds; /* per-glyph FD index when format 0, or expanded from ranges */
    uint32_t n_ranges;
} robo_font_cff_fdselect_t;

void robo_font_cff_fdselect_clear(robo_font_cff_fdselect_t *f);

robo_result_t robo_font_cff_fdselect_parse(robo_font_span_t table,
                                           size_t offset,
                                           uint32_t n_glyphs,
                                           uint32_t fd_count,
                                           robo_font_cff_fdselect_t *out);

robo_result_t robo_font_cff_fdselect_get(const robo_font_cff_fdselect_t *f,
                                         uint32_t gid,
                                         uint8_t *out_fd);

/* ---- Type 2 CharString IR ---- */

typedef enum robo_font_cff_cs_op {
    ROBO_CFF_CS_HSTEM = 1,
    ROBO_CFF_CS_VSTEM = 3,
    ROBO_CFF_CS_VMOVETO = 4,
    ROBO_CFF_CS_RLINETO = 5,
    ROBO_CFF_CS_HLINETO = 6,
    ROBO_CFF_CS_VLINETO = 7,
    ROBO_CFF_CS_RRCURVETO = 8,
    ROBO_CFF_CS_CALLSUBR = 10,
    ROBO_CFF_CS_RETURN = 11,
    ROBO_CFF_CS_ESCAPE = 12,
    ROBO_CFF_CS_ENDCHAR = 14,
    ROBO_CFF_CS_HSTEMHM = 18,
    ROBO_CFF_CS_HINTMASK = 19,
    ROBO_CFF_CS_CNTRMASK = 20,
    ROBO_CFF_CS_RMOVETO = 21,
    ROBO_CFF_CS_HMOVETO = 22,
    ROBO_CFF_CS_VSTEMHM = 23,
    ROBO_CFF_CS_RCURVELINE = 24,
    ROBO_CFF_CS_RLINECURVE = 25,
    ROBO_CFF_CS_VVCURVETO = 26,
    ROBO_CFF_CS_HHCURVETO = 27,
    ROBO_CFF_CS_CALLGSUBR = 29,
    ROBO_CFF_CS_VHCURVETO = 30,
    ROBO_CFF_CS_HVCURVETO = 31,
    /* escape */
    ROBO_CFF_CS_AND = 1203,
    ROBO_CFF_CS_OR = 1204,
    ROBO_CFF_CS_NOT = 1205,
    ROBO_CFF_CS_ABS = 1209,
    ROBO_CFF_CS_ADD = 1210,
    ROBO_CFF_CS_SUB = 1211,
    ROBO_CFF_CS_DIV = 1212,
    ROBO_CFF_CS_NEG = 1214,
    ROBO_CFF_CS_EQ = 1215,
    ROBO_CFF_CS_DROP = 1218,
    ROBO_CFF_CS_PUT = 1220,
    ROBO_CFF_CS_GET = 1221,
    ROBO_CFF_CS_IFELSE = 1222,
    ROBO_CFF_CS_RANDOM = 1223,
    ROBO_CFF_CS_MUL = 1224,
    ROBO_CFF_CS_SQRT = 1226,
    ROBO_CFF_CS_DUP = 1227,
    ROBO_CFF_CS_EXCH = 1228,
    ROBO_CFF_CS_INDEX = 1229,
    ROBO_CFF_CS_ROLL = 1230,
    ROBO_CFF_CS_HFLEX = 1234,
    ROBO_CFF_CS_FLEX = 1235,
    ROBO_CFF_CS_HFLEX1 = 1236,
    ROBO_CFF_CS_FLEX1 = 1237
} robo_font_cff_cs_op_t;

typedef struct robo_font_cff_cs_op_rec {
    uint16_t op;
    uint8_t argc;
    int32_t args[ROBO_FONT_CFF_MAX_OPERAND_STACK];
    /** For hintmask/cntrmask: mask byte count and bytes follow in mask[]. */
    uint8_t mask_bytes;
    uint8_t mask[32];
} robo_font_cff_cs_op_rec_t;

typedef struct robo_font_cff_charstring {
    uint32_t op_count;
    robo_font_cff_cs_op_rec_t *ops;
    int32_t width; /* optional width; INT32_MIN if absent */
    int has_width;
    uint32_t stem_hint_count;
} robo_font_cff_charstring_t;

void robo_font_cff_charstring_clear(robo_font_cff_charstring_t *cs);

typedef struct robo_font_cff_subr_ctx {
    const robo_font_cff_index_t *global_subrs;
    const robo_font_cff_index_t *local_subrs;
    uint32_t global_bias;
    uint32_t local_bias;
} robo_font_cff_subr_ctx_t;

/**
 * Validate and decode a Type 2 CharString into an IR.
 * Executes arithmetic/stack ops; records path/hint ops; resolves subrs.
 */
robo_result_t robo_font_cff_charstring_parse(robo_font_span_t cs_bytes,
                                             const robo_font_cff_subr_ctx_t *subrs,
                                             double default_width,
                                             double nominal_width,
                                             int has_nominal_width,
                                             robo_font_cff_charstring_t *out);

/** Like parse, with flags (e.g. CFF2 may omit endchar). */
#define ROBO_FONT_CFF_CS_FLAGS_NONE 0u
#define ROBO_FONT_CFF_CS_FLAGS_ALLOW_NO_ENDCHAR (1u << 0)

robo_result_t robo_font_cff_charstring_parse_ex(robo_font_span_t cs_bytes,
                                                const robo_font_cff_subr_ctx_t *subrs,
                                                double default_width,
                                                double nominal_width,
                                                int has_nominal_width,
                                                uint32_t flags,
                                                robo_font_cff_charstring_t *out);


/* ---- CFF2 Item Variation Store (structural) ---- */

typedef struct robo_font_cff_axis_coord {
    int16_t startCoord;
    int16_t peakCoord;
    int16_t endCoord;
} robo_font_cff_axis_coord_t;

typedef struct robo_font_cff_var_region {
    uint16_t axis_count;
    robo_font_cff_axis_coord_t *axes;
} robo_font_cff_var_region_t;

typedef struct robo_font_cff_var_data {
    uint16_t item_count;
    uint16_t short_delta_count;
    uint16_t region_index_count;
    uint16_t *region_indices;
    /* delta sets omitted as raw span for later evaluation */
    robo_font_span_t delta_sets;
} robo_font_cff_var_data_t;

typedef struct robo_font_cff_vstore {
    uint16_t format;
    uint16_t axis_count;
    uint16_t region_count;
    robo_font_cff_var_region_t *regions;
    uint16_t data_count;
    robo_font_cff_var_data_t *data;
} robo_font_cff_vstore_t;

void robo_font_cff_vstore_clear(robo_font_cff_vstore_t *v);

robo_result_t robo_font_cff_vstore_parse(robo_font_span_t table,
                                         size_t offset,
                                         robo_font_cff_vstore_t *out);

/* ---- Font DICT (CID / CFF2) ---- */

typedef struct robo_font_cff_font_dict {
    robo_font_cff_dict_t raw;
    int32_t private_size;
    int32_t private_offset;
    int32_t font_name_sid;
} robo_font_cff_font_dict_t;

void robo_font_cff_font_dict_clear(robo_font_cff_font_dict_t *f);

robo_result_t robo_font_cff_font_dict_interpret(const robo_font_cff_dict_t *raw,
                                                robo_font_cff_font_dict_t *out);

/* ---- Parsed CFF / CFF2 font ---- */

typedef struct robo_font_cff_font {
    int refs;
    robo_font_sfnt_t *sfnt;
    robo_font_outline_tech_t tech; /* CFF or CFF2 */

    uint8_t major;
    uint8_t minor;
    uint8_t hdr_size;
    uint8_t off_size;       /* CFF1 */
    uint16_t top_dict_length; /* CFF2 */

    robo_font_cff_index_t name_index;     /* CFF1 only */
    robo_font_cff_index_t top_dict_index; /* CFF1: INDEX; CFF2: single blob via length */
    robo_font_cff_index_t string_index;   /* CFF1 only */
    robo_font_cff_index_t global_subrs;
    robo_font_cff_index_t charstrings;
    robo_font_cff_index_t fdarray_index;

    robo_font_cff_top_dict_t top;
    robo_font_cff_private_dict_t priv; /* first/sole Private for non-CID */
    robo_font_cff_index_t local_subrs;

    robo_font_cff_charset_t charset;
    robo_font_cff_encoding_t encoding;
    robo_font_cff_fdselect_t fdselect;

    uint32_t fd_count;
    robo_font_cff_font_dict_t *font_dicts;
    robo_font_cff_private_dict_t *fd_privates;
    robo_font_cff_index_t *fd_local_subrs;

    robo_font_cff_vstore_t vstore;
    int has_vstore;

    uint32_t glyph_count;
} robo_font_cff_font_t;

#define ROBO_FONT_CFF_FLAGS_NONE 0u
#define ROBO_FONT_CFF_FLAGS_REQUIRE_CHARSTRINGS (1u << 0)
#define ROBO_FONT_CFF_FLAGS_PARSE_CHARSET (1u << 1)
#define ROBO_FONT_CFF_FLAGS_PARSE_ENCODING (1u << 2)
#define ROBO_FONT_CFF_FLAGS_PARSE_PRIVATE (1u << 3)
#define ROBO_FONT_CFF_FLAGS_PARSE_CID (1u << 4)
#define ROBO_FONT_CFF_FLAGS_PARSE_VSTORE (1u << 5)
#define ROBO_FONT_CFF_FLAGS_PARSE_ALL                                          \
    (ROBO_FONT_CFF_FLAGS_REQUIRE_CHARSTRINGS | ROBO_FONT_CFF_FLAGS_PARSE_CHARSET | \
     ROBO_FONT_CFF_FLAGS_PARSE_ENCODING | ROBO_FONT_CFF_FLAGS_PARSE_PRIVATE |  \
     ROBO_FONT_CFF_FLAGS_PARSE_CID | ROBO_FONT_CFF_FLAGS_PARSE_VSTORE)

/**
 * Parse CFF or CFF2 from @p sfnt based on outline detection.
 * Retains SFNT. Caller releases via robo_font_cff_release.
 */
robo_result_t robo_font_cff_parse(robo_font_sfnt_t *sfnt,
                                  uint32_t flags,
                                  robo_font_cff_font_t **out);

void robo_font_cff_retain(robo_font_cff_font_t *font);
void robo_font_cff_release(robo_font_cff_font_t *font);

robo_font_outline_tech_t robo_font_cff_outline_tech(const robo_font_cff_font_t *font);
uint32_t robo_font_cff_glyph_count(const robo_font_cff_font_t *font);

robo_result_t robo_font_cff_charstring_bytes(const robo_font_cff_font_t *font,
                                             uint32_t gid,
                                             robo_font_span_t *out);

robo_result_t robo_font_cff_parse_glyph(const robo_font_cff_font_t *font,
                                        uint32_t gid,
                                        robo_font_cff_charstring_t *out);

/** Fuzz / security entry: parse raw SFNT bytes. */
robo_result_t robo_font_cff_fuzz_parse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_CFF_H */
