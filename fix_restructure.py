"""Run from the repo root (D:\rovo-mobile) on branch mariam/restructure-project-folders:
    python fix_restructure.py
Fixes hardcoded old-folder-name path references left over after the
firmware->xaio_sw, backend->backend_sw, ai->ai_sw, verification+sil->testing rename.
"""
import csv
import io
from pathlib import Path

ROOT = Path(".").resolve()


def replace_literal(path, pairs):
    p = ROOT / path
    text = p.read_text(encoding="utf-8")
    for old, new in pairs:
        if old not in text:
            print(f"WARNING: pattern not found (already fixed or path changed?) in {path}:\n  {old!r}")
            continue
        text = text.replace(old, new)
    p.write_text(text, encoding="utf-8")
    print(f"fixed: {path}")


# 1) backend_sw/tests/test_espidf_contract.py
replace_literal("backend_sw/tests/test_espidf_contract.py", [
    ('FIRMWARE = ROOT / "firmware" / "espidf_security"',
     'FIRMWARE = ROOT / "xaio_sw" / "espidf_security"'),
])

# 2) testing/sil/run_sil.py
replace_literal("testing/sil/run_sil.py", [
    ("ROOT = Path(__file__).resolve().parents[1]",
     "ROOT = Path(__file__).resolve().parents[2]"),
    ('sys.path.insert(0, str(ROOT / "backend"))',
     'sys.path.insert(0, str(ROOT / "backend_sw"))'),
    ('FW = ROOT / "firmware"',
     'FW = ROOT / "xaio_sw"'),
    ('ap.add_argument("--out", default=str(ROOT / "sil" / "results"))',
     'ap.add_argument("--out", default=str(ROOT / "testing" / "sil" / "results"))'),
])

# 3) testing/sil/test_ble_link.py
replace_literal("testing/sil/test_ble_link.py", [
    ("ROOT = Path(__file__).resolve().parent.parent",
     "ROOT = Path(__file__).resolve().parent.parent.parent"),
    ('sys.path.insert(0, str(ROOT / "backend"))',
     'sys.path.insert(0, str(ROOT / "backend_sw"))'),
    ('open(ROOT / "verification/golden/device_reports_v2.jsonl")',
     'open(ROOT / "testing/verification/golden/device_reports_v2.jsonl")'),
])

# 4) testing/sil/compare_cameras.py
replace_literal("testing/sil/compare_cameras.py", [
    ('out = run_sil.ROOT / "sil" / "results" / "camera_comparison.md"',
     'out = run_sil.ROOT / "testing" / "sil" / "results" / "camera_comparison.md"'),
])

# 5) .github/workflows/verification.yml : fix the --unittest-log labels used for traceability matching
replace_literal(".github/workflows/verification.yml", [
    ("--unittest-log backend=backend.log --unittest-log sil=sil.log",
     "--unittest-log backend_sw=backend.log --unittest-log testing/sil=sil.log"),
])

# 6) testing/verification/check_traceability.py : full corrected rewrite
CHECK_TRACEABILITY = r'''"""Requirement -> test -> result traceability check (V-cycle, right side).

Reads the test logs written by CI (or by a local run), the test list in traceability.csv and the requirement
list in requirements.csv, and
  1. fails if a test in the code is not in traceability.csv, or a listed test no longer exists,
  2. fails if a listed test failed or did not run,
  3. fails if a test names a requirement ID that is not in requirements.csv,
  4. writes a report: every requirement with its tests and an overall result.

    (cd engine && python3 -m unittest -v 2> ../engine.log)
    (cd backend_sw && python3 -m unittest -v 2> ../backend.log)
    python3 -m unittest testing/sil/test_sil.py -v 2> sil.log
    (cd xaio_sw && pio test -e native -v > ../firmware.log && python3 tools/check_with_engine.py > ../integration.log)
    python3 testing/verification/check_traceability.py --unittest-log engine=engine.log --unittest-log backend_sw=backend.log \
        --unittest-log testing/sil=sil.log --firmware-log firmware.log --integration-log integration.log

Experiments (E-) are long or need private data, and inspections (I-) are done by a person, so they are not
rerun here; the report shows them as "recorded".
"""
import argparse
import csv
import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def code_tests() -> set[tuple[str, str]]:
    """(file, test) pairs that exist in the code."""
    found = set()
    py = [*(ROOT / 'engine/tests').glob('test_*.py'), *(ROOT / 'backend_sw/tests').glob('test_*.py'),
          *(ROOT / 'testing/sil').glob('test_*.py')]
    for f in sorted(py):
        cls = None
        for line in f.read_text().splitlines():
            if m := re.match(r'class (\w+)', line):
                cls = m.group(1)
            elif m := re.match(r'    def (test_\w+)', line):
                found.add((str(f.relative_to(ROOT)), f'{cls}.{m.group(1)}'))
    for f in sorted((ROOT / 'xaio_sw/test').glob('*/test_main.cpp')):
        for name in re.findall(r'RUN_TEST\((\w+)\)', f.read_text()):
            found.add((str(f.relative_to(ROOT)), name))
    return found


def results(unittest_logs: dict[str, str], firmware_log: str, integration_log: str | None) -> dict[tuple[str, str], str]:
    res = {}
    # "test_x (tests.module.Class.test_x) ... ok"; a test with a docstring prints the docstring on a second line.
    # Each log comes with the folder its tests were run from (engine, backend) or live in (sil).
    for base, log in unittest_logs.items():
        # a warning printed by the code under test can split "... ok" over several lines
        base_parts = base.split('/')
        for m in re.finditer(r'^(test_\w+) \(([\w.]+)\.(\w+)\.\w+\)(?:\n[^\n]*?)? \.\.\. '
                             r'(?:[^\n]*\n){0,6}?[^\n]*?\b(ok|FAIL|ERROR|skipped)\b', log, re.M):
            name, module, cls, outcome = m.groups()
            parts = module.split('.')
            if parts[:len(base_parts)] == base_parts:
                parts = parts[len(base_parts):]
            res[(f'{base}/{"/".join(parts)}.py', f'{cls}.{name}')] = 'Pass' if outcome == 'ok' else 'Fail'
    for m in re.finditer(r'^test/(\w+)/test_main\.cpp:\d+:(\w+):(PASS|FAIL|IGNORE)', firmware_log, re.M):
        suite, name, outcome = m.groups()
        res[(f'xaio_sw/test/{suite}/test_main.cpp', name)] = 'Pass' if outcome == 'PASS' else 'Fail'
    if integration_log is not None:
        lines = [l for l in integration_log.splitlines() if 'signature:' in l]
        ok = bool(lines) and all('engine: valid' in l and 'signature: ok' in l and l.rstrip().endswith('True')
                                 for l in lines)
        res[('xaio_sw/tools/check_with_engine.py', '*')] = 'Pass' if ok else 'Fail'
    return res


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--unittest-log', action='append', required=True, metavar='FOLDER=LOG',
                    help='Python unittest -v output, e.g. engine=engine.log (repeat for each suite)')
    ap.add_argument('--firmware-log', required=True)
    ap.add_argument('--integration-log')
    ap.add_argument('--out', default='traceability_report.md')
    a = ap.parse_args()

    reqs = {r['id']: r for r in csv.DictReader(open(HERE / 'requirements.csv'))}
    items = list(csv.DictReader(open(HERE / 'traceability.csv')))
    logs = {k: Path(v).read_text() for k, v in (x.split('=', 1) for x in a.unittest_log)}
    res = results(logs, Path(a.firmware_log).read_text(),
                  Path(a.integration_log).read_text() if a.integration_log else None)
    errors = []

    kind = lambda i: i['test_id'].split('-')[0]
    listed = {(i['file'], i['test']) for i in items if kind(i) in ('T', 'F', 'B', 'S')}
    existing = code_tests()
    for f, t in sorted(existing - listed):
        errors.append(f'test not in traceability.csv: {f} {t}')
    for f, t in sorted(listed - existing):
        errors.append(f'listed test no longer exists: {f} {t}')

    by_req = defaultdict(list)
    for i in items:
        k = kind(i)
        if k in ('T', 'F', 'B', 'S'):
            i['result'] = res.get((i['file'], i['test']), 'Not run')
        elif k == 'IT':
            i['result'] = res.get((i['file'], '*'), 'Not run')
        else:
            i['result'] = 'Recorded'
        if i['result'] in ('Fail', 'Not run'):
            errors.append(f"{i['test_id']} {i['test']}: {i['result']}")
        for r in i['requirements'].split():
            if r.startswith('('):
                continue
            if r not in reqs:
                errors.append(f"{i['test_id']} names unknown requirement {r}")
            by_req[r].append(i)

    def verdict(tests):
        if not tests:
            return 'Not verified'
        if any(t['result'] == 'Fail' for t in tests):
            return 'Fail'
        if all(t['result'] == 'Recorded' for t in tests):
            return 'Recorded (experiment / inspection)'
        return 'Pass'

    counts = defaultdict(lambda: defaultdict(int))
    rows = []
    for rid, r in reqs.items():
        v = verdict(by_req.get(rid, []))
        counts[r['set']][v] += 1
        ids = ', '.join(t['test_id'] for t in by_req.get(rid, []))
        rows.append(f"| {rid} | {r['priority']} | {v} | {ids} |")

    n_auto = sum(1 for i in items if kind(i) in ('T', 'F', 'B', 'S', 'IT'))
    n_pass = sum(1 for i in items if i['result'] == 'Pass')
    lines = ['# ICVSP traceability report', '',
             f'{n_pass} of {n_auto} automated verification items pass; '
             f"{sum(1 for i in items if kind(i) == 'E')} experiments and "
             f"{sum(1 for i in items if kind(i) == 'I')} inspections recorded.", '',
             '| Requirement set | Pass | Recorded (experiment / inspection) | Fail | Not verified |', '|---|---|---|---|---|']
    for s in ('system', 'security'):
        c = counts[s]
        lines.append(f"| {s} | {c['Pass']} | {c['Recorded (experiment / inspection)']} | {c['Fail']} | {c['Not verified']} |")
    lines += ['', '| Requirement | Priority | Result | Verified by |', '|---|---|---|---|', *rows]
    if errors:
        lines += ['', '## Problems', '', *[f'- {e}' for e in errors]]
    Path(a.out).write_text('\n'.join(lines) + '\n')
    print('\n'.join(lines[:8]))
    for e in errors:
        print('ERROR:', e, file=sys.stderr)
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
'''
(ROOT / "testing/verification/check_traceability.py").write_text(CHECK_TRACEABILITY, encoding="utf-8")
print("fixed: testing/verification/check_traceability.py (full rewrite)")

# 7) testing/verification/traceability.csv : fix the 'file' column prefixes (robust CSV parsing, handles quoted fields)
csv_path = ROOT / "testing/verification/traceability.csv"
with csv_path.open(newline="", encoding="utf-8") as fh:
    rows = list(csv.reader(fh))
header, data_rows = rows[0], rows[1:]
file_col = header.index("file")

PREFIX_MAP = [
    ("backend/", "backend_sw/"),
    ("firmware/", "xaio_sw/"),
    ("ai/", "ai_sw/"),
    ("sil/", "testing/sil/"),
]


def fix_file_cell(cell: str) -> str:
    parts = [p.strip() for p in cell.split(",")]
    fixed = []
    for part in parts:
        if part == "sil":
            part = "testing/sil"
        else:
            for old, new in PREFIX_MAP:
                if part.startswith(old):
                    part = new + part[len(old):]
                    break
        fixed.append(part)
    return ", ".join(fixed)


changed = 0
for row in data_rows:
    new_val = fix_file_cell(row[file_col])
    if new_val != row[file_col]:
        changed += 1
    row[file_col] = new_val

out = io.StringIO()
writer = csv.writer(out)
writer.writerow(header)
writer.writerows(data_rows)
csv_path.write_text(out.getvalue(), encoding="utf-8", newline="")
print(f"fixed: testing/verification/traceability.csv ({changed} rows updated)")

print("\nDone. Now review with: git diff --stat")
