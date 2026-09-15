#!/usr/bin/env python3
"""Verify the pipeline's machine-readable artifacts.

Checks that every results JSON parses, that the JUnit XML parses, and that no
artifact carries a UTF-8 BOM (a BOM makes `json.load` fail outright).

Usage: python scripts/pipeline/dev/check-artifacts.py [repo-root]
"""

import glob
import json
import os
import sys
import xml.etree.ElementTree as ET

BOM = b"\xef\xbb\xbf"


def main() -> int:
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    failures = 0

    patterns = [
        ("results JSON", os.path.join(root, "build", "test-results", "*.json")),
        ("JUnit XML", os.path.join(root, "build", "test-results", "*.junit.xml")),
        ("deploy manifest", os.path.join(root, "build", "deploy", "deployment.json")),
    ]

    for label, pattern in patterns:
        paths = sorted(glob.glob(pattern))
        if not paths:
            print(f"  --   no {label} found ({pattern})")
            continue
        for path in paths:
            name = os.path.basename(path)
            raw = open(path, "rb").read()
            bom = raw.startswith(BOM)
            try:
                if path.endswith(".json"):
                    doc = json.loads(raw.decode("utf-8"))
                    extra = ""
                    if isinstance(doc, dict) and "passed" in doc:
                        extra = f"passed={doc['passed']} failed={doc['failed']} ok={doc['ok']}"
                else:
                    node = ET.fromstring(raw.decode("utf-8"))
                    extra = f"tests={node.attrib.get('tests')} failures={node.attrib.get('failures')}"
            except Exception as exc:  # noqa: BLE001 - report, do not raise
                print(f"  BAD  {label:14} {name:34} {exc}")
                failures += 1
                continue
            flag = "BOM " if bom else "     "
            if bom:
                failures += 1
            print(f"  {flag} {label:14} {name:34} {extra}")

    print()
    if failures:
        print(f"FAIL {failures} artifact problem(s)")
        return 1
    print("PASS every artifact parses and carries no BOM")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
