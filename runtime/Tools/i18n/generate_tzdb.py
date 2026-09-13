#!/usr/bin/env python3
"""
Generate RoboI18N tzdb tables from IANA Time Zone Database (Goal 4.14).

Primary input: Tools/i18n/source/tzdb-<TZDB_VERSION>/
  - Zone text (africa, asia, …, backward) for Links / zone.tab
  - Optional zoneinfo/ TZif tree (preferred)

If zoneinfo/ is missing, copies TZif files from the installed Python
`tzdata` package (IANA-derived) into zoneinfo/ for offline parsing.
Normal Build.ps1 never runs this script and never needs network.

Outputs:
  Include/RoboI18N/RoboTzData.h
  Source/RoboI18N/data/robo_tz_data.c
  Tools/i18n/tzdb_golden_hashes.json
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import struct
import sys
from dataclasses import dataclass, field
from datetime import datetime, timedelta, timezone
from pathlib import Path

from robo_i18n_version import GENERATOR_VERSION, ROBO_I18N_VERSION, TZDB_VERSION

HERE = Path(__file__).resolve().parent
RUNTIME = HERE.parent.parent
SRC = HERE / "source" / f"tzdb-{TZDB_VERSION}"
OUT_H = RUNTIME / "Include" / "RoboI18N" / "RoboTzData.h"
OUT_C = RUNTIME / "Source" / "RoboI18N" / "data" / "robo_tz_data.c"
GOLDEN = HERE / "tzdb_golden_hashes.json"

# Documented supported civil range for generated transitions (seconds).
RANGE_START = datetime(1800, 1, 1, tzinfo=timezone.utc)
RANGE_END = datetime(2100, 1, 1, tzinfo=timezone.utc)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def c_escape(s: str) -> str:
    out: list[str] = []
    for b in s.encode("utf-8"):
        if 32 <= b < 127 and b not in (34, 92):
            out.append(chr(b))
        else:
            out.append(f"\\{b:03o}")
    return "".join(out)


@dataclass
class Trans:
    unix_sec: int
    offset: int
    is_dst: int
    abbr: str


@dataclass
class ZoneData:
    id: str
    canonical: str
    transitions: list[Trans] = field(default_factory=list)
    # Offset in force before first transition (or forever if empty).
    initial_offset: int = 0
    initial_dst: int = 0
    initial_abbr: str = "UTC"


def ensure_zoneinfo() -> Path:
    zi = SRC / "zoneinfo"
    if zi.is_dir() and any(zi.rglob("*")):
        return zi
    try:
        import tzdata  # type: ignore
    except ImportError as e:
        raise SystemExit(
            "zoneinfo/ missing and Python tzdata package not installed; "
            "run Fetch-Tzdb.ps1 or: pip install tzdata"
        ) from e
    pkg = Path(tzdata.__file__).resolve().parent / "zoneinfo"
    if not pkg.is_dir():
        raise SystemExit(f"tzdata package has no zoneinfo at {pkg}")
    print(f"Copying TZif tree from {pkg} → {zi}")
    if zi.exists():
        shutil.rmtree(zi)
    shutil.copytree(pkg, zi)
    return zi


def parse_links(src: Path) -> dict[str, str]:
    """Map alias → target from IANA zone files (Link lines)."""
    links: dict[str, str] = {}
    files = [
        "africa",
        "antarctica",
        "asia",
        "australasia",
        "europe",
        "northamerica",
        "southamerica",
        "etcetera",
        "backward",
        "factory",
    ]
    for name in files:
        path = src / name
        if not path.is_file():
            continue
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            line = line.split("#", 1)[0].strip()
            if not line.startswith("Link"):
                continue
            parts = line.split()
            if len(parts) < 3:
                continue
            # Link TARGET ALIAS
            target, alias = parts[1], parts[2]
            links[alias] = target
    return links


def resolve_canonical(name: str, links: dict[str, str]) -> str:
    seen: set[str] = set()
    cur = name
    while cur in links and cur not in seen:
        seen.add(cur)
        cur = links[cur]
    return cur


def read_zone_tab(src: Path) -> list[str]:
    path = src / "zone1970.tab"
    if not path.is_file():
        path = src / "zone.tab"
    zones: list[str] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) >= 3:
            zones.append(parts[2])
    # Always include UTC / Etc zones used by tests.
    for z in ("UTC", "Etc/UTC", "Etc/GMT"):
        if z not in zones:
            zones.append(z)
    return sorted(set(zones))


def _parse_posix_time(s: str, i: int) -> tuple[int, int]:
    """Parse [/]hh[:mm[:ss]] → seconds since midnight (may be >86400), new index."""
    if i < len(s) and s[i] == "/":
        i += 1
    sign = 1
    if i < len(s) and s[i] in "+-":
        sign = -1 if s[i] == "-" else 1
        i += 1
    if i >= len(s) or not s[i].isdigit():
        return 2 * 3600, i  # default 02:00
    h = 0
    while i < len(s) and s[i].isdigit():
        h = h * 10 + (ord(s[i]) - 48)
        i += 1
    m = 0
    sec = 0
    if i < len(s) and s[i] == ":":
        i += 1
        m = 0
        while i < len(s) and s[i].isdigit():
            m = m * 10 + (ord(s[i]) - 48)
            i += 1
        if i < len(s) and s[i] == ":":
            i += 1
            sec = 0
            while i < len(s) and s[i].isdigit():
                sec = sec * 10 + (ord(s[i]) - 48)
                i += 1
    return sign * (h * 3600 + m * 60 + sec), i


def _parse_posix_offset(s: str, i: int) -> tuple[int, int]:
    """
    Parse POSIX std/dst offset → seconds *east of UTC*, new index.
    POSIX: positive means west of UTC (hours added to local to get UTC).
    """
    sign = 1  # POSIX default positive = west
    if i < len(s) and s[i] in "+-":
        sign = -1 if s[i] == "-" else 1
        i += 1
    if i >= len(s) or not s[i].isdigit():
        raise ValueError(f"bad POSIX offset at {i}: {s}")
    h = 0
    while i < len(s) and s[i].isdigit():
        h = h * 10 + (ord(s[i]) - 48)
        i += 1
    m = 0
    sec = 0
    if i < len(s) and s[i] == ":":
        i += 1
        while i < len(s) and s[i].isdigit():
            m = m * 10 + (ord(s[i]) - 48)
            i += 1
        if i < len(s) and s[i] == ":":
            i += 1
            while i < len(s) and s[i].isdigit():
                sec = sec * 10 + (ord(s[i]) - 48)
                i += 1
    posix_secs = sign * (h * 3600 + m * 60 + sec)
    return -posix_secs, i  # convert to east-of-UTC


def _parse_posix_abbr(s: str, i: int) -> tuple[str, int]:
    if i < len(s) and s[i] == "<":
        j = s.find(">", i + 1)
        if j < 0:
            raise ValueError(f"unclosed <abbr> in {s}")
        return s[i + 1 : j], j + 1
    j = i
    while j < len(s) and s[j].isalpha():
        j += 1
    if j == i:
        raise ValueError(f"missing abbr in {s} at {i}")
    return s[i:j], j


@dataclass
class _PosixDateRule:
    kind: str  # 'J' | 'N' | 'M'
    a: int = 0
    b: int = 0
    c: int = 0
    time_sec: int = 2 * 3600


@dataclass
class _PosixTz:
    std_abbr: str
    std_offset: int
    dst_abbr: str | None = None
    dst_offset: int | None = None
    start: _PosixDateRule | None = None
    end: _PosixDateRule | None = None


def parse_posix_tz(s: str) -> _PosixTz | None:
    """Parse TZif footer POSIX TZ string. Empty → None."""
    s = s.strip()
    if not s:
        return None
    i = 0
    std_abbr, i = _parse_posix_abbr(s, i)
    std_off, i = _parse_posix_offset(s, i)
    tz = _PosixTz(std_abbr=std_abbr, std_offset=std_off)
    if i >= len(s):
        return tz
    # DST abbr
    try:
        dst_abbr, j = _parse_posix_abbr(s, i)
    except ValueError:
        return tz
    i = j
    tz.dst_abbr = dst_abbr
    tz.dst_offset = std_off + 3600
    if i < len(s) and (s[i].isdigit() or s[i] in "+-"):
        tz.dst_offset, i = _parse_posix_offset(s, i)
    if i >= len(s) or s[i] != ",":
        return tz
    i += 1

    def parse_date(idx: int) -> tuple[_PosixDateRule, int]:
        if idx >= len(s):
            raise ValueError(f"truncated date rule in {s}")
        if s[idx] == "M":
            idx += 1
            m = 0
            while idx < len(s) and s[idx].isdigit():
                m = m * 10 + (ord(s[idx]) - 48)
                idx += 1
            if idx >= len(s) or s[idx] != ".":
                raise ValueError(s)
            idx += 1
            n = 0
            while idx < len(s) and s[idx].isdigit():
                n = n * 10 + (ord(s[idx]) - 48)
                idx += 1
            if idx >= len(s) or s[idx] != ".":
                raise ValueError(s)
            idx += 1
            d = 0
            while idx < len(s) and s[idx].isdigit():
                d = d * 10 + (ord(s[idx]) - 48)
                idx += 1
            tsec, idx = _parse_posix_time(s, idx)
            return _PosixDateRule("M", m, n, d, tsec), idx
        if s[idx] == "J":
            idx += 1
            n = 0
            while idx < len(s) and s[idx].isdigit():
                n = n * 10 + (ord(s[idx]) - 48)
                idx += 1
            tsec, idx = _parse_posix_time(s, idx)
            return _PosixDateRule("J", n, 0, 0, tsec), idx
        n = 0
        while idx < len(s) and s[idx].isdigit():
            n = n * 10 + (ord(s[idx]) - 48)
            idx += 1
        tsec, idx = _parse_posix_time(s, idx)
        return _PosixDateRule("N", n, 0, 0, tsec), idx

    start, i = parse_date(i)
    if i >= len(s) or s[i] != ",":
        raise ValueError(f"expected end rule in {s}")
    i += 1
    end, i = parse_date(i)
    tz.start = start
    tz.end = end
    return tz


def _nth_weekday_of_month(year: int, month: int, week: int, weekday: int) -> datetime:
    """week 1..5 (5=last), weekday POSIX 0=Sun..6=Sat → naive date at midnight."""
    py_wd = (weekday + 6) % 7  # Sun→6, Mon→0, …
    if week == 5:
        if month == 12:
            d = datetime(year + 1, 1, 1) - timedelta(days=1)
        else:
            d = datetime(year, month + 1, 1) - timedelta(days=1)
        while d.weekday() != py_wd:
            d -= timedelta(days=1)
        return d
    d = datetime(year, month, 1)
    while d.weekday() != py_wd:
        d += timedelta(days=1)
    d += timedelta(weeks=week - 1)
    # week N may spill into next month for N=5 only; for 1..4 clamp if needed
    if d.month != month:
        # Should not happen for week 1..4 in Gregorian; keep last in-month weekday.
        d -= timedelta(weeks=1)
    return d


def _posix_rule_local_dt(year: int, rule: _PosixDateRule) -> datetime:
    if rule.kind == "M":
        day = _nth_weekday_of_month(year, rule.a, rule.b, rule.c)
        return day + timedelta(seconds=rule.time_sec)
    if rule.kind == "J":
        # Jn: day of year 1..365, Feb 29 never counted
        n = rule.a
        if n < 1 or n > 365:
            raise ValueError(f"bad Jn {n}")
        d = datetime(year, 1, 1) + timedelta(days=n - 1)
        leap = year % 4 == 0 and (year % 100 != 0 or year % 400 == 0)
        if n >= 60 and leap:
            d += timedelta(days=1)
        return d + timedelta(seconds=rule.time_sec)
    # n: 0..365 including Feb 29 in leap years
    return datetime(year, 1, 1) + timedelta(days=rule.a, seconds=rule.time_sec)


def expand_posix_transitions(
    tz: _PosixTz, after_sec: int, end_sec: int
) -> list[Trans]:
    """Generate DST transitions strictly after after_sec, up to end_sec."""
    if not tz.dst_abbr or tz.start is None or tz.end is None or tz.dst_offset is None:
        return []
    out: list[Trans] = []
    epoch = datetime(1970, 1, 1)
    try:
        y0 = max(1800, (epoch + timedelta(seconds=after_sec)).year - 1)
    except OverflowError:
        y0 = 1800
    try:
        y1 = min(2100, (epoch + timedelta(seconds=end_sec)).year + 1)
    except OverflowError:
        y1 = 2100

    std_off = tz.std_offset
    dst_off = tz.dst_offset
    for year in range(y0, y1 + 1):
        local_start = _posix_rule_local_dt(year, tz.start)
        local_end = _posix_rule_local_dt(year, tz.end)
        # Treat civil fields as if UTC, then subtract the pre-transition offset.
        utc_start = int(local_start.replace(tzinfo=timezone.utc).timestamp()) - std_off
        utc_end = int(local_end.replace(tzinfo=timezone.utc).timestamp()) - dst_off

        for sec, off, dst, abbr in (
            (utc_start, dst_off, 1, tz.dst_abbr),
            (utc_end, std_off, 0, tz.std_abbr),
        ):
            if sec <= after_sec or sec > end_sec:
                continue
            out.append(Trans(sec, off, dst, abbr))
    out.sort(key=lambda t: t.unix_sec)
    dedup: list[Trans] = []
    for t in out:
        if dedup and dedup[-1].unix_sec == t.unix_sec:
            dedup[-1] = t
        else:
            dedup.append(t)
    return dedup


def parse_tzif(path: Path) -> ZoneData:
    data = path.read_bytes()
    if len(data) < 44 or data[0:4] != b"TZif":
        raise ValueError(f"not TZif: {path}")
    version = data[4:5]
    tz_footer = ""
    # Use 64-bit section when present (version 2+).
    if version in (b"2", b"3", b"4"):
        (
            _tzh_ttisutcnt,
            _tzh_ttisstdcnt,
            tzh_leapcnt,
            tzh_timecnt,
            tzh_typecnt,
            tzh_charcnt,
        ) = struct.unpack(">6i", data[20:44])
        off = 44
        off += tzh_timecnt * 4
        off += tzh_timecnt
        off += tzh_typecnt * 6
        off += tzh_charcnt
        off += tzh_leapcnt * 8
        off += _tzh_ttisstdcnt
        off += _tzh_ttisutcnt
        if data[off : off + 4] != b"TZif":
            raise ValueError(f"missing v2 header in {path}")
        (
            tzh_ttisutcnt,
            tzh_ttisstdcnt,
            tzh_leapcnt,
            tzh_timecnt,
            tzh_typecnt,
            tzh_charcnt,
        ) = struct.unpack(">6i", data[off + 20 : off + 44])
        off += 44
        times = list(struct.unpack(f">{tzh_timecnt}q", data[off : off + 8 * tzh_timecnt]))
        off += 8 * tzh_timecnt
        types = list(data[off : off + tzh_timecnt])
        off += tzh_timecnt
        ttinfo = []
        for i in range(tzh_typecnt):
            gmtoff, isdst, abbrind = struct.unpack(">lbb", data[off : off + 6])
            ttinfo.append((gmtoff, isdst, abbrind))
            off += 6
        abbrs = data[off : off + tzh_charcnt]
        off += tzh_charcnt
        off += tzh_leapcnt * 12  # 8-byte time + 4-byte correction
        off += tzh_ttisstdcnt
        off += tzh_ttisutcnt
        if off < len(data) and data[off] == 0x0A:
            off += 1
            nl = data.find(b"\n", off)
            if nl >= 0:
                tz_footer = data[off:nl].decode("ascii", errors="replace")
    else:
        (
            tzh_ttisutcnt,
            tzh_ttisstdcnt,
            tzh_leapcnt,
            tzh_timecnt,
            tzh_typecnt,
            tzh_charcnt,
        ) = struct.unpack(">6i", data[20:44])
        off = 44
        times = list(struct.unpack(f">{tzh_timecnt}i", data[off : off + 4 * tzh_timecnt]))
        off += 4 * tzh_timecnt
        types = list(data[off : off + tzh_timecnt])
        off += tzh_timecnt
        ttinfo = []
        for i in range(tzh_typecnt):
            gmtoff, isdst, abbrind = struct.unpack(">lbb", data[off : off + 6])
            ttinfo.append((gmtoff, isdst, abbrind))
            off += 6
        abbrs = data[off : off + tzh_charcnt]

    def abbr_at(idx: int) -> str:
        end = abbrs.find(b"\0", idx)
        if end < 0:
            end = len(abbrs)
        return abbrs[idx:end].decode("ascii", errors="replace")

    init_off, init_dst, init_ai = ttinfo[0] if ttinfo else (0, 0, 0)
    zd = ZoneData(
        id="",
        canonical="",
        initial_offset=int(init_off),
        initial_dst=int(init_dst),
        initial_abbr=abbr_at(init_ai),
    )
    start_sec = int(RANGE_START.timestamp())
    end_sec = int(RANGE_END.timestamp())
    last_explicit = None
    for i, t in enumerate(times):
        if t < start_sec:
            ti = types[i]
            gmtoff, isdst, abbrind = ttinfo[ti]
            zd.initial_offset = int(gmtoff)
            zd.initial_dst = int(isdst)
            zd.initial_abbr = abbr_at(abbrind)
            last_explicit = int(t)
            continue
        if t > end_sec:
            break
        ti = types[i]
        gmtoff, isdst, abbrind = ttinfo[ti]
        zd.transitions.append(
            Trans(int(t), int(gmtoff), int(isdst), abbr_at(abbrind))
        )
        last_explicit = int(t)

    # Expand POSIX TZ footer for transitions after the last explicit one.
    posix = parse_posix_tz(tz_footer) if tz_footer else None
    if posix is not None:
        after = last_explicit if last_explicit is not None else (start_sec - 1)
        # If no explicit transitions in range, still expand from after.
        if zd.transitions:
            after = zd.transitions[-1].unix_sec
        elif last_explicit is not None:
            after = last_explicit
        extra = expand_posix_transitions(posix, after, end_sec)
        zd.transitions.extend(extra)
    return zd


def find_tzif(zi_root: Path, zone_id: str) -> Path | None:
    p = zi_root / zone_id
    if p.is_file():
        return p
    return None


def build_tables() -> tuple[list[ZoneData], dict[str, str], list[str]]:
    if not SRC.is_dir():
        raise SystemExit(f"missing tzdb source: {SRC}")
    links = parse_links(SRC)
    zone_ids = read_zone_tab(SRC)
    # Include all link aliases as named ids.
    for alias in links:
        if alias not in zone_ids:
            zone_ids.append(alias)
    zone_ids = sorted(set(zone_ids))

    zi = ensure_zoneinfo()
    zones: list[ZoneData] = []
    # Deduplicate by canonical physical zone file.
    canonical_cache: dict[str, ZoneData] = {}
    alias_map: dict[str, str] = {}

    for zid in zone_ids:
        canon = resolve_canonical(zid, links)
        alias_map[zid] = canon
        if canon in canonical_cache:
            continue
        path = find_tzif(zi, canon)
        if path is None:
            # Some Etc/GMT±N may only exist as links.
            path = find_tzif(zi, zid)
        if path is None:
            print(f"WARN skip (no TZif): {zid} → {canon}", file=sys.stderr)
            continue
        zd = parse_tzif(path)
        zd.id = canon
        zd.canonical = canon
        canonical_cache[canon] = zd
        zones.append(zd)

    # Ensure UTC exists.
    if "UTC" not in canonical_cache and "Etc/UTC" in canonical_cache:
        u = canonical_cache["Etc/UTC"]
        u2 = ZoneData(
            id="UTC",
            canonical="UTC",
            transitions=list(u.transitions),
            initial_offset=u.initial_offset,
            initial_dst=u.initial_dst,
            initial_abbr=u.initial_abbr,
        )
        zones.append(u2)
        canonical_cache["UTC"] = u2
        alias_map["UTC"] = "UTC"

    zones.sort(key=lambda z: z.id)
    return zones, alias_map, zone_ids


def emit(zones: list[ZoneData], alias_map: dict[str, str]) -> tuple[str, str]:
    # String pool for ids + abbreviations.
    pool: list[str] = []
    pool_index: dict[str, int] = {}

    def intern(s: str) -> int:
        if s not in pool_index:
            pool_index[s] = len(pool)
            pool.append(s)
        return pool_index[s]

    for z in zones:
        intern(z.id)
        intern(z.initial_abbr)
        for t in z.transitions:
            intern(t.abbr)
    for a, c in alias_map.items():
        intern(a)
        intern(c)

    # Flatten transitions.
    all_trans: list[tuple[int, int, int, int]] = []  # sec, offset, dst, abbr_idx
    zone_rows: list[tuple[int, int, int, int, int, int]] = []
    # id_idx, initial_off, initial_dst, initial_abbr, trans_start, trans_count

    for z in zones:
        start = len(all_trans)
        for t in z.transitions:
            all_trans.append((t.unix_sec, t.offset, t.is_dst, intern(t.abbr)))
        zone_rows.append(
            (
                intern(z.id),
                z.initial_offset,
                z.initial_dst,
                intern(z.initial_abbr),
                start,
                len(z.transitions),
            )
        )

    aliases = sorted((a, c) for a, c in alias_map.items() if a != c)

    h = f"""/* Auto-generated by generate_tzdb.py — DO NOT EDIT.
 * IANA tzdb {TZDB_VERSION}. Generator {GENERATOR_VERSION}. API {ROBO_I18N_VERSION}.
 * Leap seconds: not modeled (POSIX 86400-second days).
 * Transition civil range: 1800-01-01 .. 2100-01-01 UTC.
 */
#ifndef ROBO_TZ_DATA_H
#define ROBO_TZ_DATA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {{
#endif

#define ROBO_TZDB_VERSION_STRING "{TZDB_VERSION}"
#define ROBO_TZ_ZONE_COUNT {len(zone_rows)}u
#define ROBO_TZ_ALIAS_COUNT {len(aliases)}u
#define ROBO_TZ_TRANS_COUNT {len(all_trans)}u
#define ROBO_TZ_ABBR_MAX 16u

typedef struct robo_tz_zone {{
    int32_t id;           /* string pool index */
    int32_t initial_offset;
    uint8_t initial_dst;
    int32_t initial_abbr; /* string pool index */
    uint32_t trans_start;
    uint32_t trans_count;
}} robo_tz_zone_t;

typedef struct robo_tz_trans {{
    int64_t unix_sec;     /* transition instant (UTC) */
    int32_t offset;       /* offset east of UTC after transition */
    uint8_t is_dst;
    int32_t abbr;         /* string pool index */
}} robo_tz_trans_t;

typedef struct robo_tz_alias {{
    int32_t alias_id;
    int32_t canonical_id;
}} robo_tz_alias_t;

extern const char robo_tz_string_pool[];
extern const uint32_t robo_tz_string_pool_size;
extern const int32_t robo_tz_string_offsets[];
extern const uint32_t robo_tz_string_count;

extern const robo_tz_zone_t robo_tz_zones[];
extern const robo_tz_trans_t robo_tz_trans[];
extern const robo_tz_alias_t robo_tz_aliases[];

const char *robo_tz_string(int32_t idx);
const char *robo_tzdb_version_string(void);
const char *robo_tz_generator_version(void);

#ifdef __cplusplus
}}
#endif

#endif /* ROBO_TZ_DATA_H */
"""

    # Build string pool blob.
    offsets: list[int] = []
    blob = bytearray()
    for s in pool:
        offsets.append(len(blob))
        blob.extend(s.encode("utf-8"))
        blob.append(0)

    def pool_c() -> str:
        lines = ["static const char robo_tz_pool_storage[] ="]
        # Emit as concatenation of string literals chunks.
        chunk: list[str] = []
        size = 0
        for s in pool:
            lit = '"' + c_escape(s) + '\\0"'
            chunk.append(lit)
            size += len(s) + 1
            if size > 400:
                lines.append("  " + " ".join(chunk))
                chunk = []
                size = 0
        if chunk:
            lines.append("  " + " ".join(chunk))
        lines.append(";")
        return "\n".join(lines)

    c_parts: list[str] = []
    c_parts.append(
        f"""/* Auto-generated by generate_tzdb.py — DO NOT EDIT.
 * IANA tzdb {TZDB_VERSION}. Generator {GENERATOR_VERSION}.
 */
#include "RoboI18N/RoboTzData.h"

{pool_c()}

const char robo_tz_string_pool[] = {{0}}; /* unused; accessors use storage */
const uint32_t robo_tz_string_pool_size = {len(blob)}u;
"""
    )
    # Fix: use storage as the pool. Simpler approach:
    c_parts = []
    c_parts.append(
        f"""/* Auto-generated by generate_tzdb.py — DO NOT EDIT.
 * IANA tzdb {TZDB_VERSION}. Generator {GENERATOR_VERSION}.
 */
#include "RoboI18N/RoboTzData.h"

"""
    )
    # Emit offsets + join pool as one big string via octets for reliability.
    c_parts.append("static const unsigned char robo_tz_pool_bytes[] = {\n")
    for i in range(0, len(blob), 16):
        chunk = blob[i : i + 16]
        c_parts.append("  " + ", ".join(f"0x{b:02x}" for b in chunk) + ",\n")
    c_parts.append("};\n\n")
    c_parts.append(
        f"const char *const robo_tz_string_pool_ptr = (const char *)robo_tz_pool_bytes;\n"
        f"const uint32_t robo_tz_string_pool_size = {len(blob)}u;\n"
        f"const char robo_tz_string_pool[] = {{0}}; /* ABI placeholder */\n\n"
    )
    c_parts.append("const int32_t robo_tz_string_offsets[] = {\n")
    for i, off in enumerate(offsets):
        c_parts.append(f"  {off}," + ("\n" if (i % 8) == 7 else ""))
    c_parts.append("\n};\n")
    c_parts.append(f"const uint32_t robo_tz_string_count = {len(offsets)}u;\n\n")

    c_parts.append("const robo_tz_zone_t robo_tz_zones[] = {\n")
    for row in zone_rows:
        c_parts.append(
            "  {%d, %d, %du, %d, %du, %du},\n"
            % (row[0], row[1], row[2], row[3], row[4], row[5])
        )
    c_parts.append("};\n\n")

    c_parts.append("const robo_tz_trans_t robo_tz_trans[] = {\n")
    if not all_trans:
        c_parts.append("  {0, 0, 0u, 0},\n")
    for t in all_trans:
        c_parts.append("  {%dLL, %d, %du, %d},\n" % (t[0], t[1], t[2], t[3]))
    c_parts.append("};\n\n")

    c_parts.append("const robo_tz_alias_t robo_tz_aliases[] = {\n")
    if not aliases:
        c_parts.append("  {0, 0},\n")
    for a, cname in aliases:
        c_parts.append("  {%d, %d},\n" % (intern(a), intern(cname)))
    c_parts.append("};\n\n")

    c_parts.append(
        f"""
const char *robo_tz_string(int32_t idx)
{{
    if (idx < 0 || (uint32_t)idx >= robo_tz_string_count) {{
        return "";
    }}
    return (const char *)robo_tz_pool_bytes + robo_tz_string_offsets[idx];
}}

const char *robo_tzdb_version_string(void)
{{
    return ROBO_TZDB_VERSION_STRING;
}}

const char *robo_tz_generator_version(void)
{{
    return "{GENERATOR_VERSION}";
}}
"""
    )

    # Fix header: remove unused pool extern confusion — keep string() API.
    h = h.replace(
        "extern const char robo_tz_string_pool[];\nextern const uint32_t robo_tz_string_pool_size;\n",
        "extern const uint32_t robo_tz_string_pool_size;\n",
    )

    return h, "".join(c_parts)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--write-golden", action="store_true")
    ap.add_argument("--repro", action="store_true")
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    zones, alias_map, all_ids = build_tables()
    print(f"zones={len(zones)} aliases={sum(1 for a,c in alias_map.items() if a!=c)} ids={len(all_ids)}")

    # Validation
    ids = [z.id for z in zones]
    if len(ids) != len(set(ids)):
        raise SystemExit("duplicate canonical zone ids")
    for z in zones:
        prev = None
        for t in z.transitions:
            if prev is not None and t.unix_sec <= prev:
                raise SystemExit(f"unordered transitions in {z.id}")
            prev = t.unix_sec
            if abs(t.offset) > 18 * 3600:
                raise SystemExit(f"offset out of range in {z.id}: {t.offset}")

    h, c = emit(zones, alias_map)
    OUT_H.parent.mkdir(parents=True, exist_ok=True)
    OUT_C.parent.mkdir(parents=True, exist_ok=True)

    if args.repro or args.check:
        if not OUT_H.is_file() or not OUT_C.is_file():
            raise SystemExit("missing outputs for --repro/--check")
        if OUT_H.read_text(encoding="utf-8") != h or OUT_C.read_text(encoding="utf-8") != c:
            raise SystemExit("repro mismatch: regenerate with generate_tzdb.py")
        print("OK repro")
        if args.check and GOLDEN.is_file():
            g = json.loads(GOLDEN.read_text(encoding="utf-8"))
            if g.get("RoboTzData.h") != sha256_bytes(h.encode("utf-8")):
                raise SystemExit("golden hash mismatch: RoboTzData.h")
            if g.get("robo_tz_data.c") != sha256_bytes(c.encode("utf-8")):
                raise SystemExit("golden hash mismatch: robo_tz_data.c")
            print("OK golden")
        return

    OUT_H.write_text(h, encoding="utf-8", newline="\n")
    OUT_C.write_text(c, encoding="utf-8", newline="\n")
    print(f"Wrote {OUT_H}")
    print(f"Wrote {OUT_C}")

    if args.write_golden:
        g = {
            "tzdb_version": TZDB_VERSION,
            "generator": GENERATOR_VERSION,
            "RoboTzData.h": sha256_bytes(h.encode("utf-8")),
            "robo_tz_data.c": sha256_bytes(c.encode("utf-8")),
        }
        GOLDEN.write_text(json.dumps(g, indent=2) + "\n", encoding="utf-8")
        print(f"Wrote {GOLDEN}")


if __name__ == "__main__":
    main()
