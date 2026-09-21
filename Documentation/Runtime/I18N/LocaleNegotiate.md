# Locale Negotiation & Accept-Language (Goal 4.17)

Language: **C**. This subsystem performs **locale negotiation**, not language
detection. It does not inspect text content.

Builds on Goal 4.16 (`RoboI18NLocaleService`) and Goal 4.1 locale tags.

## Concepts

| Term | Meaning |
|---|---|
| Language range | Preference: BCP 47 tag or `*` |
| Preference list | Ordered ranges with quality weights |
| Supported locale | Application-available locale (canonicalized) |
| Negotiation | Pick best supported locale for preferences |
| Resource fallback | Goal 4.16 data-parent chain (after selection) |

Negotiation selects a supported locale. Resource lookup may then fall back
from that locale. Do not run two competing fallback systems during matching.

## Quality weights (`q`)

Stored as **millipercent** integers `0..1000` (RFC 7231 ≤3 fractional digits).

| Value | Meaning |
|---|---|
| 1000 | `q=1` (default when omitted) |
| 900 | `q=0.9` |
| 0 | Exclusion — matching locales must not be selected |

Equal `q`: stable order (earlier input wins after normalize dedupe).
Duplicates (same range identity): **highest-q wins**; equal q → **first-wins**.

## Language ranges

Accepted:

* `*`
* Canonical BCP 47 tags (`en`, `en-US`, `zh-Hans`, `ur-PK`, …)
* Optional `;q=`

Rejected (parse error):

* Extended forms with `*` inside (`*-CH`, `en-*`)
* `q` outside `0..1` or >3 fractional digits
* Malformed tags / non-ASCII language tags

## Matching rules

`I18nLocaleMatchQuality(range, supported)`:

1. Languages must be equal (case-normalized).
2. **Script conflict**: after CLDR likely-subtag maximize on both sides, if both
   have scripts and they differ → **no match** (`zh-Hant` ≉ `zh-Hans`).
3. Else rank:
   * **exact** — core identity equal
   * **region** — same region
   * **script** — same explicit/maximized script as requested
   * **language** — language only
   * **wildcard** — `*`

Region preference: `pt-BR` prefers `pt-BR` over `pt-PT` when both are supported.

## Negotiation algorithm

`I18nLocaleNegotiateEx`:

1. Canonicalize supported locales (copy; no caller mutation).
2. Walk normalized preferences with `q>0` in order.
3. For each preference, score all non-excluded supported locales; pick best
   kind; ties → **lowest supported index**.
4. `q=0` ranges exclude matching supported locales.
5. `*` selects the first non-excluded supported locale.

Deterministic: no hash-table iteration.

## Accept-Language

```c
I18nPreferenceList *prefs = NULL;
I18nAcceptLanguageParse(
    "en-US,en;q=0.9,ur-PK;q=0.8", 28, &prefs);
I18nPreferenceListNormalize(prefs);
/* serialize → parse preserves semantics (not byte-identical) */
```

Invalid entries are **skipped**. Oversize header (`>2048`) → error.
Limits: 32 ranges, 256 supported locales.

## Examples

```text
User:     ur-PK, en-US;q=0.8
Supports: en-US, ur, ar
Result:   ur   (language match)

User:     zh-Hant-TW
Supports: zh-Hans, zh-TW, en
Result:   zh-TW  (region; Hans rejected by script conflict)
```

## Security

Treat Accept-Language as untrusted. Bounds above apply. Fuzz suite covers
malformed headers. No unbounded allocations.

## API

See `RoboI18NLocaleNegotiate.h`. Legacy `I18nLocaleNegotiate` (Goal 4.16)
delegates to this engine.
