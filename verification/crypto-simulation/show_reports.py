"""Inspect locally accepted reports for the Phase 2 simulation (not an HTTP API)."""
import argparse
from pathlib import Path
import sqlite3


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--db', default='data/cloud_reports.sqlite3')
    args = parser.parse_args()
    path = Path(args.db)
    if not path.exists():
        parser.error(f'Database not found: {path}. Start the cloud simulator first.')
    with sqlite3.connect(path) as db:
        rows = db.execute(
            'SELECT device_id, message_id, hazard_type, latitude, longitude, '
            'detected_at, received_at FROM reports ORDER BY received_at'
        ).fetchall()
    print(f'[Cloud] stored unique reports: {len(rows)}')
    for row in rows:
        print('  device=%s | message=%s | %s | lat=%.7f lon=%.7f | detected=%s | received=%s' % row)


if __name__ == '__main__':
    main()
