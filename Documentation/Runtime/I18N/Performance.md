# Performance notes (Goal 4.27)

Phase 4 prioritizes correctness over micro-optimization. Host suite includes
`i18n.p4.perf_smoke` / conformance perf smoke (thousands of format/plural ops).

## Characteristics

| Operation | Expectation |
|---|---|
| Locale parse / resolve | O(tag length + small table lookups); no heap for locale values |
| Number / currency format | O(digits); stack buffers; optional RoboString grow |
| Plural select | O(1) bytecode walk on immutable tables |
| Date/time format | Pattern compiled once per formatter |
| Timezone lookup | Binary search / indexed TZif tables |
| Collation / search | Depends on strength; avoid repeated collator create in hot loops |
| Transliteration | Rule scan; streaming hold buffer capped |
| Message format | Parse once (`mf_parse`) then format; one-shot template reparses |

## Avoided anti-patterns

* Runtime XML/JSON CLDR parsing
* Per-call locale re-maximize when settings already resolved
* Accidental O(n²) UTF-8 rescans in format loops (not observed on smoke loads)

## Data size (source tables, approximate)

Largest committed generated sources: display names (~4.6 MB), UCA (~2.1 MB),
tzdb (~1.2 MB). Coverage is intentional; do not strip locales solely for size
without a product decision.
