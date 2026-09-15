/*
 * Named character-reference catalogue (Phase 8.2).
 *
 * Entries are grouped by category (core, punctuation, currency, Latin-1
 * letters, symbols, math) and the order carries no meaning: the engine
 * (RoboHTMLCharRef.c) scans the whole table and applies the HTML
 * longest-match rule by name length, so "&amp;" beats "&amp" regardless
 * of position. Names are matched byte-wise against the source, so an
 * uppercase name such as "Dagger" is case-sensitive by construction.
 * See RoboHTMLCharRefData.h.
 */
#include "RoboHTMLCharRefData.h"

const robo_html_charref_entry_t robo_html_charref_table[] = {
    { "amp", 38u, 0u },        /* &       */
    { "amp;", 38u, 0u },
    { "apos", 39u, 0u },       /* '       */
    { "apos;", 39u, 0u },
    { "gt", 62u, 0u },         /* >       */
    { "gt;", 62u, 0u },
    { "lt", 60u, 0u },         /* <       */
    { "lt;", 60u, 0u },
    { "quot", 34u, 0u },       /* "       */
    { "quot;", 34u, 0u },
    { "nbsp", 160u, 0u },      /* NBSP    */
    { "nbsp;", 160u, 0u },
    { "copy", 169u, 0u },      /* ©      */
    { "copy;", 169u, 0u },
    { "reg", 174u, 0u },       /* ®      */
    { "reg;", 174u, 0u },
    { "trade", 8482u, 0u },    /* ™      */
    { "trade;", 8482u, 0u },
    { "hellip", 8230u, 0u },   /* …     */
    { "hellip;", 8230u, 0u },
    { "NotEqualTilde;", 8770u, 824u }, /* ≂̸ (U+2242 U+0338: two code points) */
    { "mdash", 8212u, 0u },    /* —     */
    { "mdash;", 8212u, 0u },
    { "ndash", 8211u, 0u },    /* –     */
    { "ndash;", 8211u, 0u },
    { "lsquo", 8216u, 0u },    /* ‘     */
    { "lsquo;", 8216u, 0u },
    { "rsquo", 8217u, 0u },    /* ’     */
    { "rsquo;", 8217u, 0u },
    { "ldquo", 8220u, 0u },    /* “     */
    { "ldquo;", 8220u, 0u },
    { "rdquo", 8221u, 0u },    /* ”     */
    { "rdquo;", 8221u, 0u },
    { "bull", 8226u, 0u },     /* •     */
    { "bull;", 8226u, 0u },
    { "middot", 183u, 0u },    /* ·      */
    { "middot;", 183u, 0u },
    { "laquo", 171u, 0u },     /* «     */
    { "laquo;", 171u, 0u },
    { "raquo", 187u, 0u },     /* »     */
    { "raquo;", 187u, 0u },
    { "deg", 176u, 0u },       /* °       */
    { "deg;", 176u, 0u },
    { "plusmn", 177u, 0u },    /* ±     */
    { "plusmn;", 177u, 0u },
    { "times", 215u, 0u },     /* ×     */
    { "times;", 215u, 0u },
    { "divide", 247u, 0u },    /* ÷    */
    { "divide;", 247u, 0u },
    { "micro", 181u, 0u },     /* µ      */
    { "micro;", 181u, 0u },
    { "para", 182u, 0u },      /* ¶       */
    { "para;", 182u, 0u },
    { "sect", 167u, 0u },      /* §       */
    { "sect;", 167u, 0u },
    { "dagger", 8224u, 0u },   /* †     */
    { "dagger;", 8224u, 0u },
    { "Dagger", 8225u, 0u },   /* ‡ (exact-case: names match byte-wise) */
    { "Dagger;", 8225u, 0u },
    { "euro", 8364u, 0u },     /* €      */
    { "euro;", 8364u, 0u },
    { "pound", 163u, 0u },     /* £      */
    { "pound;", 163u, 0u },
    { "yen", 165u, 0u },       /* ¥       */
    { "yen;", 165u, 0u },
    { "cent", 162u, 0u },      /* ¢      */
    { "cent;", 162u, 0u },
    { "curren", 164u, 0u },
    { "curren;", 164u, 0u },
    { "aacute", 225u, 0u },
    { "aacute;", 225u, 0u },
    { "agrave", 224u, 0u },
    { "agrave;", 224u, 0u },
    { "acirc", 226u, 0u },
    { "acirc;", 226u, 0u },
    { "auml", 228u, 0u },
    { "auml;", 228u, 0u },
    { "aring", 229u, 0u },
    { "aring;", 229u, 0u },
    { "aelig", 230u, 0u },
    { "aelig;", 230u, 0u },
    { "ccedil", 231u, 0u },
    { "ccedil;", 231u, 0u },
    { "eacute", 233u, 0u },
    { "eacute;", 233u, 0u },
    { "egrave", 232u, 0u },
    { "egrave;", 232u, 0u },
    { "ecirc", 234u, 0u },
    { "ecirc;", 234u, 0u },
    { "euml", 235u, 0u },
    { "euml;", 235u, 0u },
    { "iacute", 237u, 0u },
    { "iacute;", 237u, 0u },
    { "igrave", 236u, 0u },
    { "igrave;", 236u, 0u },
    { "iuml", 239u, 0u },
    { "iuml;", 239u, 0u },
    { "ntilde", 241u, 0u },
    { "ntilde;", 241u, 0u },
    { "oacute", 243u, 0u },
    { "oacute;", 243u, 0u },
    { "ograve", 242u, 0u },
    { "ograve;", 242u, 0u },
    { "ocirc", 244u, 0u },
    { "ocirc;", 244u, 0u },
    { "ouml", 246u, 0u },
    { "ouml;", 246u, 0u },
    { "oslash", 248u, 0u },
    { "oslash;", 248u, 0u },
    { "uacute", 250u, 0u },
    { "uacute;", 250u, 0u },
    { "ugrave", 249u, 0u },
    { "ugrave;", 249u, 0u },
    { "uuml", 252u, 0u },
    { "uuml;", 252u, 0u },
    { "yuml", 255u, 0u },
    { "yuml;", 255u, 0u },
    { "szlig", 223u, 0u },
    { "szlig;", 223u, 0u },
    { "alpha", 945u, 0u },
    { "alpha;", 945u, 0u },
    { "beta", 946u, 0u },
    { "beta;", 946u, 0u },
    { "gamma", 947u, 0u },
    { "gamma;", 947u, 0u },
    { "delta", 948u, 0u },
    { "delta;", 948u, 0u },
    { "pi", 960u, 0u },
    { "pi;", 960u, 0u },
    { "sigma", 963u, 0u },
    { "sigma;", 963u, 0u },
    { "omega", 969u, 0u },
    { "omega;", 969u, 0u },
    { "Omega", 937u, 0u },
    { "infin", 8734u, 0u },    /* ∞     */
    { "infin;", 8734u, 0u },
    { "ne", 8800u, 0u },       /* ≠       */
    { "ne;", 8800u, 0u },
    { "le", 8804u, 0u },
    { "le;", 8804u, 0u },
    { "ge", 8805u, 0u },
    { "ge;", 8805u, 0u },
    { "asymp", 8776u, 0u },
    { "asymp;", 8776u, 0u },
    { "radic", 8730u, 0u },
    { "radic;", 8730u, 0u },
    { "sum", 8721u, 0u },
    { "sum;", 8721u, 0u },
    { "prod", 8719u, 0u },
    { "prod;", 8719u, 0u },
    { "int", 8747u, 0u },
    { "int;", 8747u, 0u },
    { "harr", 8596u, 0u },
    { "harr;", 8596u, 0u },
    { "larr", 8592u, 0u },
    { "larr;", 8592u, 0u },
    { "uarr", 8593u, 0u },
    { "uarr;", 8593u, 0u },
    { "rarr", 8594u, 0u },
    { "rarr;", 8594u, 0u },
    { "darr", 8595u, 0u },
    { "darr;", 8595u, 0u },
    { "spades", 9824u, 0u },
    { "spades;", 9824u, 0u },
    { "clubs", 9827u, 0u },
    { "clubs;", 9827u, 0u },
    { "hearts", 9829u, 0u },
    { "hearts;", 9829u, 0u },
    { "diams", 9830u, 0u },
    { "diams;", 9830u, 0u },
    { "loz", 9671u, 0u },
    { "loz;", 9671u, 0u },
    { "crarr", 8629u, 0u },
    { "crarr;", 8629u, 0u },
    { "shy", 173u, 0u },
    { "shy;", 173u, 0u },
    { "ensp", 8194u, 0u },
    { "ensp;", 8194u, 0u },
    { "emsp", 8195u, 0u },
    { "emsp;", 8195u, 0u },
    { "thinsp", 8201u, 0u },
    { "thinsp;", 8201u, 0u },
    { "zwnj", 8204u, 0u },
    { "zwnj;", 8204u, 0u },
    { "zwj", 8205u, 0u },
    { "zwj;", 8205u, 0u },
    { "lrm", 8206u, 0u },
    { "lrm;", 8206u, 0u },
    { "rlm", 8207u, 0u },
    { "rlm;", 8207u, 0u },
    { "sbquo", 8218u, 0u },
    { "sbquo;", 8218u, 0u },
    { "bdquo", 8222u, 0u },
    { "bdquo;", 8222u, 0u },
    { "lsaquo", 8249u, 0u },
    { "lsaquo;", 8249u, 0u },
    { "rsaquo", 8250u, 0u },
    { "rsaquo;", 8250u, 0u },
    { "oline", 8254u, 0u },
    { "oline;", 8254u, 0u },
    { "frasl", 8260u, 0u },
    { "frasl;", 8260u, 0u },
    { "weierp", 8472u, 0u },
    { "weierp;", 8472u, 0u },
    { "image", 8465u, 0u },
    { "image;", 8465u, 0u },
    { "real", 8476u, 0u },
    { "real;", 8476u, 0u },
    { "alefsym", 8501u, 0u },
    { "alefsym;", 8501u, 0u },
    { "sdot", 8901u, 0u },
    { "sdot;", 8901u, 0u },
    { "lceil", 8968u, 0u },
    { "lceil;", 8968u, 0u },
    { "rceil", 8969u, 0u },
    { "rceil;", 8969u, 0u },
    { "lfloor", 8970u, 0u },
    { "lfloor;", 8970u, 0u },
    { "rfloor", 8971u, 0u },
    { "rfloor;", 8971u, 0u },
    { "lang", 10216u, 0u },
    { "lang;", 10216u, 0u },
    { "rang", 10217u, 0u },
    { "rang;", 10217u, 0u },
    { "OElig", 338u, 0u },
    { "oelig", 339u, 0u },
    { "Scaron", 352u, 0u },
    { "scaron", 353u, 0u },
    { "Yuml", 376u, 0u },
    { "fnof", 402u, 0u },
    { "circ", 710u, 0u },
    { "circ;", 710u, 0u },
    { "tilde", 732u, 0u },
    { "tilde;", 732u, 0u }
};

const size_t robo_html_charref_table_size =
    sizeof(robo_html_charref_table) / sizeof(robo_html_charref_table[0]);
