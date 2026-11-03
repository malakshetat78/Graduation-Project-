"""Storage for local runs and the system simulation: SQLite (one small file, or memory for tests). Production
uses pg_store.PgStore on Supabase, with the same methods; backend/tests/test_postgres.py runs the API tests on it."""
from __future__ import annotations

import json
import sqlite3
import threading
from contextlib import contextmanager

SCHEMA = """
CREATE TABLE IF NOT EXISTS devices (
    device_id   TEXT PRIMARY KEY,
    public_key  TEXT NOT NULL,              -- Ed25519, hex
    status      TEXT NOT NULL DEFAULT 'ACTIVE',  -- ACTIVE / REVOKED
    good        REAL NOT NULL DEFAULT 0,    -- Beta reputation counts (trust engine)
    bad         REAL NOT NULL DEFAULT 0,
    pairing_code_hash TEXT,                 -- one-time, cleared on use
    registered  REAL NOT NULL
);
CREATE TABLE IF NOT EXISTS device_counters (
    device_id TEXT PRIMARY KEY, highest_boot_id INTEGER NOT NULL, highest_sequence INTEGER NOT NULL,
    reports_stored INTEGER NOT NULL, last_report_at REAL NOT NULL
);
CREATE TABLE IF NOT EXISTS reports (
    device_id   TEXT NOT NULL,
    event_id    TEXT NOT NULL,
    boot_id     INTEGER NOT NULL,
    sequence    INTEGER NOT NULL,
    detected_at REAL NOT NULL,              -- Unix seconds
    received_at REAL NOT NULL,
    line        TEXT NOT NULL,              -- the signed report exactly as received
    settled     INTEGER NOT NULL DEFAULT 0, -- reputation already updated from this report
    evaluation_status TEXT NOT NULL DEFAULT 'pending',
    evaluation_reasons TEXT NOT NULL DEFAULT '[]',
    evaluated_at REAL,
    PRIMARY KEY (device_id, event_id),
    UNIQUE (device_id, boot_id, sequence)
);
CREATE TABLE IF NOT EXISTS beacons (
    vehicle_id  TEXT NOT NULL,
    t           REAL NOT NULL,
    lat REAL NOT NULL, lon REAL NOT NULL, speed_mps REAL NOT NULL, heading_deg REAL NOT NULL,
    detector_on INTEGER NOT NULL,
    PRIMARY KEY (vehicle_id, t)
);
CREATE TABLE IF NOT EXISTS evaluation_runs (
    run_id INTEGER PRIMARY KEY AUTOINCREMENT, ran_at REAL NOT NULL, config_hash TEXT NOT NULL,
    reports INTEGER NOT NULL, pending INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS hazards (
    hazard_id TEXT PRIMARY KEY, type TEXT NOT NULL, state TEXT NOT NULL, severity TEXT NOT NULL,
    latitude REAL NOT NULL, longitude REAL NOT NULL, heading_deg REAL NOT NULL,
    first_seen REAL NOT NULL, expires_at REAL NOT NULL, independent_sources INTEGER NOT NULL,
    net_evidence REAL NOT NULL, sybil_suspected INTEGER NOT NULL, reasons TEXT NOT NULL,
    last_run_id INTEGER NOT NULL, updated_at REAL NOT NULL
);
CREATE TABLE IF NOT EXISTS hazard_evidence (
    hazard_id TEXT NOT NULL, device_id TEXT NOT NULL, event_id TEXT NOT NULL,
    c REAL NOT NULL, g REAL NOT NULL, h REAL NOT NULL, f REAL NOT NULL, weight REAL NOT NULL,
    PRIMARY KEY (hazard_id, device_id, event_id)
);
CREATE TABLE IF NOT EXISTS hazard_passes (
    hazard_id TEXT NOT NULL, device_id TEXT NOT NULL,
    c REAL NOT NULL, g REAL NOT NULL, h REAL NOT NULL, f REAL NOT NULL, weight REAL NOT NULL,
    PRIMARY KEY (hazard_id, device_id)
);
CREATE TABLE IF NOT EXISTS reputation_events (    -- each outcome counted once per device and hazard
    device_id TEXT NOT NULL, hazard_id TEXT NOT NULL, kind TEXT NOT NULL,
    good_delta REAL NOT NULL, bad_delta REAL NOT NULL, created_at REAL NOT NULL,
    PRIMARY KEY (device_id, hazard_id, kind)
);
CREATE TABLE IF NOT EXISTS rejections (
    received_at REAL NOT NULL, device_id TEXT, event_id TEXT, code TEXT NOT NULL, reason TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS profiles (
    user_id TEXT PRIMARY KEY, role TEXT NOT NULL DEFAULT 'driver'
);
CREATE TABLE IF NOT EXISTS user_devices (
    user_id TEXT NOT NULL, device_id TEXT NOT NULL, linked_at REAL NOT NULL, PRIMARY KEY (user_id, device_id)
);
CREATE TABLE IF NOT EXISTS alert_deliveries (
    user_id TEXT NOT NULL, hazard_id TEXT NOT NULL, last_alerted_at REAL NOT NULL, PRIMARY KEY (user_id, hazard_id)
);
CREATE INDEX IF NOT EXISTS reports_time ON reports (detected_at);
CREATE INDEX IF NOT EXISTS reports_pending ON reports (evaluation_status);
CREATE INDEX IF NOT EXISTS beacons_time ON beacons (t);
"""


class Store:
    def __init__(self, path: str = ":memory:"):
        self.db = sqlite3.connect(path, check_same_thread=False)
        self.db.row_factory = sqlite3.Row
        self.lock = threading.RLock()
        self.depth = 0
        columns = {c["name"] for c in self.db.execute("PRAGMA table_info(reports)")}
        if columns and "evaluation_status" not in columns:
            self.db.close()
            raise RuntimeError(f"{path} was made by an older version of the backend: delete it, or set ICVSP_DB to a "
                               "new file. It only holds local development data.")
        with self.lock:
            self.db.executescript(SCHEMA)

    def close(self) -> None:
        self.db.close()

    def ping(self) -> None:
        self._x("SELECT 1")

    @contextmanager
    def transaction(self):
        """Everything inside commits together or not at all."""
        with self.lock:
            self.depth += 1
            try:
                yield
            except BaseException:
                if self.depth == 1:
                    self.db.rollback()
                raise
            else:
                if self.depth == 1:
                    self.db.commit()
            finally:
                self.depth -= 1

    def try_evaluator_lock(self) -> bool:
        return True   # one process, and transaction() already holds the lock

    def _x(self, sql: str, args=()) -> sqlite3.Cursor:
        with self.lock:
            cur = self.db.execute(sql, args)
            if self.depth == 0:
                self.db.commit()
            return cur

    # devices
    def add_device(self, device_id: str, public_key: str, now: float, good: float = 0.0,
                   pairing_code_hash: str | None = None) -> bool:
        """False if the device ID or the key is already registered."""
        if self._x("SELECT 1 FROM devices WHERE public_key = ?", (public_key,)).fetchone():
            return False
        return self._x("INSERT INTO devices (device_id, public_key, registered, good, pairing_code_hash) "
                       "VALUES (?, ?, ?, ?, ?) ON CONFLICT DO NOTHING",
                       (device_id, public_key, now, good, pairing_code_hash)).rowcount == 1

    def device(self, device_id: str) -> sqlite3.Row | None:
        return self._x("SELECT * FROM devices WHERE device_id = ?", (device_id,)).fetchone()

    def set_status(self, device_id: str, status: str) -> bool:
        return self._x("UPDATE devices SET status = ? WHERE device_id = ?", (status, device_id)).rowcount == 1

    def set_pairing_code(self, device_id: str, code_hash: str) -> bool:
        return self._x("UPDATE devices SET pairing_code_hash = ? WHERE device_id = ?",
                       (code_hash, device_id)).rowcount == 1

    def reputations(self) -> dict[str, tuple[float, float]]:
        return {r["device_id"]: (r["good"], r["bad"]) for r in self._x("SELECT device_id, good, bad FROM devices")}

    def devices(self) -> list[sqlite3.Row]:
        return self._x("SELECT device_id, status, good, bad, registered FROM devices ORDER BY device_id").fetchall()

    def add_reputation_event(self, device_id: str, hazard_id: str, kind: str, good: float, bad: float,
                             now: float) -> bool:
        """Applies the change only the first time this outcome is seen; False if it was already counted."""
        with self.transaction():
            cur = self._x("INSERT INTO reputation_events VALUES (?, ?, ?, ?, ?, ?) ON CONFLICT DO NOTHING",
                          (device_id, hazard_id, kind, good, bad, now))
            if cur.rowcount != 1:
                return False
            self._x("UPDATE devices SET good = good + ?, bad = bad + ? WHERE device_id = ?", (good, bad, device_id))
            return True

    def counters(self, device_id: str) -> sqlite3.Row | None:
        return self._x("SELECT * FROM device_counters WHERE device_id = ?", (device_id,)).fetchone()

    # reports
    def report(self, device_id: str, event_id: str) -> sqlite3.Row | None:
        return self._x("SELECT * FROM reports WHERE device_id = ? AND event_id = ?", (device_id, event_id)).fetchone()

    def add_report(self, ev: dict, line: str, detected_at: float, sent_at: float, now: float) -> bool:
        """Stores the report and the device's counter state together. False, with nothing changed, if the
        event_id or (device, boot, sequence) is already there."""
        with self.transaction():
            cur = self._x("INSERT INTO reports (device_id, event_id, boot_id, sequence, detected_at, received_at, line) "
                          "VALUES (?, ?, ?, ?, ?, ?, ?) ON CONFLICT DO NOTHING",
                          (ev["device_id"], ev["event_id"], ev["boot_id"], ev["sequence"], detected_at, now, line))
            if cur.rowcount != 1:
                return False
            self._x("""INSERT INTO device_counters VALUES (?, ?, ?, 1, ?)
                       ON CONFLICT (device_id) DO UPDATE SET
                         highest_sequence = CASE
                           WHEN excluded.highest_boot_id > highest_boot_id THEN excluded.highest_sequence
                           WHEN excluded.highest_boot_id = highest_boot_id
                             THEN MAX(highest_sequence, excluded.highest_sequence)
                           ELSE highest_sequence END,
                         highest_boot_id = MAX(highest_boot_id, excluded.highest_boot_id),
                         reports_stored = reports_stored + 1, last_report_at = excluded.last_report_at""",
                    (ev["device_id"], ev["boot_id"], ev["sequence"], now))
            return True

    def reports_since(self, t: float) -> list[sqlite3.Row]:
        return self._x("SELECT * FROM reports WHERE detected_at >= ? ORDER BY detected_at", (t,)).fetchall()

    def device_reports(self, device_id: str, limit: int) -> list[sqlite3.Row]:
        return self._x("SELECT * FROM reports WHERE device_id = ? ORDER BY detected_at DESC LIMIT ?",
                       (device_id, limit)).fetchall()

    def report_hazards(self, device_id: str, event_id: str) -> list[str]:
        return [r["hazard_id"] for r in self._x(
            "SELECT hazard_id FROM hazard_evidence WHERE device_id = ? AND event_id = ?", (device_id, event_id))]

    def set_report_evaluation(self, device_id: str, event_id: str, status: str, reasons: list[str], now: float) -> None:
        self._x("UPDATE reports SET evaluation_status = ?, evaluation_reasons = ?, evaluated_at = ? "
                "WHERE device_id = ? AND event_id = ?", (status, json.dumps(reasons), now, device_id, event_id))

    def close_pending_before(self, t: float, now: float) -> None:
        self._x("UPDATE reports SET evaluation_status = 'evaluated', evaluated_at = ?, "
                "evaluation_reasons = '[\"aged out before evaluation\"]' "
                "WHERE evaluation_status = 'pending' AND detected_at < ?", (now, t))

    def pending_count(self) -> int:
        return self._x("SELECT COUNT(*) FROM reports WHERE evaluation_status = 'pending'").fetchone()[0]

    def mark_settled(self, device_id: str, event_id: str) -> None:
        self._x("UPDATE reports SET settled = 1 WHERE device_id = ? AND event_id = ?", (device_id, event_id))

    def reject(self, now: float, device_id: str | None, event_id: str | None, code: str, reason: str) -> None:
        self._x("INSERT INTO rejections VALUES (?, ?, ?, ?, ?)", (now, device_id, event_id, code, reason))

    def rejections(self, limit: int = -1) -> list[sqlite3.Row]:
        """Oldest first; with a limit, only the latest `limit` rows."""
        rows = self._x("SELECT * FROM rejections ORDER BY received_at DESC LIMIT ?", (limit,)).fetchall()
        return rows[::-1]

    def recent_reports(self, limit: int) -> list[sqlite3.Row]:
        return self._x("SELECT * FROM reports ORDER BY received_at DESC LIMIT ?", (limit,)).fetchall()

    def count(self, table: str) -> int:
        assert table in ("devices", "reports", "rejections", "beacons", "hazards")
        return self._x(f"SELECT COUNT(*) FROM {table}").fetchone()[0]

    # beacons
    def add_beacons(self, rows: list[tuple]) -> None:
        with self.lock:
            self.db.executemany("INSERT OR IGNORE INTO beacons VALUES (?, ?, ?, ?, ?, ?, ?)", rows)
            if self.depth == 0:
                self.db.commit()

    def beacons_since(self, t: float) -> list[sqlite3.Row]:
        return self._x("SELECT * FROM beacons WHERE t >= ? ORDER BY t", (t,)).fetchall()

    def latest_positions(self, since: float, until: float) -> list[sqlite3.Row]:
        """The newest beacon of every vehicle seen between `since` and `until`."""
        return self._x("SELECT b.* FROM beacons b JOIN (SELECT vehicle_id, MAX(t) AS t FROM beacons "
                       "WHERE t BETWEEN ? AND ? GROUP BY vehicle_id) m ON b.vehicle_id = m.vehicle_id AND b.t = m.t",
                       (since, until)).fetchall()

    # trust engine results
    def add_run(self, now: float, config_hash: str, reports: int, pending: int) -> int:
        return self._x("INSERT INTO evaluation_runs (ran_at, config_hash, reports, pending) VALUES (?, ?, ?, ?)",
                       (now, config_hash, reports, pending)).lastrowid

    def active_hazards(self) -> list[dict]:
        """Hazards not yet expired, each with the (device, event) keys of its evidence."""
        out = {r["hazard_id"]: {**dict(r), "evidence": set()}
               for r in self._x("SELECT * FROM hazards WHERE state <> 'expired' ORDER BY first_seen, hazard_id")}
        for r in self._x("SELECT hazard_id, device_id, event_id FROM hazard_evidence"):
            if r["hazard_id"] in out:
                out[r["hazard_id"]]["evidence"].add((r["device_id"], r["event_id"]))
        return list(out.values())

    def save_hazard(self, h: dict, support: list[dict], passes: list[dict]) -> None:
        """Replaces the hazard's current state and its evidence rows."""
        with self.transaction():
            self._x("""INSERT INTO hazards VALUES (:hazard_id, :type, :state, :severity, :latitude, :longitude,
                         :heading_deg, :first_seen, :expires_at, :independent_sources, :net_evidence,
                         :sybil_suspected, :reasons_json, :last_run_id, :updated_at)
                       ON CONFLICT (hazard_id) DO UPDATE SET state = excluded.state, severity = excluded.severity,
                         latitude = excluded.latitude, longitude = excluded.longitude,
                         heading_deg = excluded.heading_deg, first_seen = MIN(first_seen, excluded.first_seen),
                         expires_at = excluded.expires_at, independent_sources = excluded.independent_sources,
                         net_evidence = excluded.net_evidence, sybil_suspected = excluded.sybil_suspected,
                         reasons = excluded.reasons, last_run_id = excluded.last_run_id,
                         updated_at = excluded.updated_at""",
                    {**h, "reasons_json": json.dumps(h["reasons"]), "sybil_suspected": int(h["sybil_suspected"])})
            self._x("DELETE FROM hazard_evidence WHERE hazard_id = ?", (h["hazard_id"],))
            self._x("DELETE FROM hazard_passes WHERE hazard_id = ?", (h["hazard_id"],))
            for e in support:
                self._x("INSERT INTO hazard_evidence VALUES (:hazard_id, :device_id, :event_id, :c, :g, :h, :f, :weight)",
                        {**e, "hazard_id": h["hazard_id"]})
            for e in passes:
                self._x("INSERT INTO hazard_passes VALUES (:hazard_id, :device_id, :c, :g, :h, :f, :weight)",
                        {**e, "hazard_id": h["hazard_id"]})

    def expire_hazards_except(self, keep: list[str], now: float) -> None:
        marks = ",".join("?" * len(keep))
        self._x(f"UPDATE hazards SET state = 'expired', expires_at = ?, updated_at = ? "
                f"WHERE state <> 'expired' AND hazard_id NOT IN ({marks})", (now, now, *keep))

    def hazard(self, hazard_id: str) -> dict | None:
        r = self._x("SELECT * FROM hazards WHERE hazard_id = ?", (hazard_id,)).fetchone()
        return {**dict(r), "reasons": json.loads(r["reasons"])} if r else None

    def hazard_evidence(self, hazard_id: str) -> dict:
        return {"support": [dict(r) for r in self._x("SELECT * FROM hazard_evidence WHERE hazard_id = ?", (hazard_id,))],
                "passes": [dict(r) for r in self._x("SELECT * FROM hazard_passes WHERE hazard_id = ?", (hazard_id,))]}

    def nearby_hazards(self, lat: float, lon: float, radius_m: float, states: tuple[str, ...], now: float) -> list[dict]:
        """Valid hazards in these states within the radius. PgStore does this with PostGIS."""
        from icvsp.geo import distance_m
        marks = ",".join("?" * len(states))
        out = []
        for r in self._x(f"SELECT * FROM hazards WHERE state IN ({marks}) AND expires_at > ?", (*states, now)):
            dist = distance_m(lat, lon, r["latitude"], r["longitude"])
            if dist <= radius_m:
                out.append({**dict(r), "reasons": json.loads(r["reasons"]), "distance_m": dist})
        return sorted(out, key=lambda h: h["distance_m"])

    # users
    def add_profile(self, user_id: str, role: str = "driver") -> None:
        """Supabase does this with a trigger on auth.users."""
        self._x("INSERT INTO profiles VALUES (?, ?) ON CONFLICT DO NOTHING", (user_id, role))

    def profile(self, user_id: str) -> sqlite3.Row | None:
        return self._x("SELECT * FROM profiles WHERE user_id = ?", (user_id,)).fetchone()

    def set_role(self, user_id: str, role: str) -> bool:
        return self._x("UPDATE profiles SET role = ? WHERE user_id = ?", (role, user_id)).rowcount == 1

    def user_devices(self, user_id: str) -> list[str]:
        return [r["device_id"] for r in self._x(
            "SELECT device_id FROM user_devices WHERE user_id = ? ORDER BY device_id", (user_id,))]

    def owns_device(self, user_id: str, device_id: str) -> bool:
        return self._x("SELECT 1 FROM user_devices WHERE user_id = ? AND device_id = ?",
                       (user_id, device_id)).fetchone() is not None

    def owned_among(self, user_id: str, device_ids: list[str]) -> set[str]:
        return set(self.user_devices(user_id)) & set(device_ids)

    def claim_device(self, user_id: str, device_id: str, code_hash: str, now: float) -> bool:
        """Spends the one-time pairing code and links the device, together."""
        with self.transaction():
            cur = self._x("UPDATE devices SET pairing_code_hash = NULL WHERE device_id = ? AND pairing_code_hash = ? "
                          "AND status = 'ACTIVE'", (device_id, code_hash))
            if cur.rowcount != 1:
                return False
            self._x("INSERT INTO user_devices VALUES (?, ?, ?) ON CONFLICT DO NOTHING", (user_id, device_id, now))
            return True

    def claim_alert(self, user_id: str, hazard_id: str, now: float, cooldown_s: float) -> bool:
        """True if this user may be alerted about this hazard now, recording it; False inside the cooldown."""
        return self._x("""INSERT INTO alert_deliveries VALUES (?, ?, ?)
                          ON CONFLICT (user_id, hazard_id) DO UPDATE SET last_alerted_at = excluded.last_alerted_at
                          WHERE last_alerted_at <= ?""", (user_id, hazard_id, now, now - cooldown_s)).rowcount == 1

    # retention
    def purge(self, beacons_before: float, keep_near: dict[str, float], near_deg: float, lead_s: float,
              reports_before: float, rejections_before: float, hazards_before: float) -> None:
        """Positions older than `beacons_before` stay only if near a report still counted (`keep_near`: its type -> the
        oldest report of that type still counted)."""
        counted = " OR ".join("(json_extract(r.line, '$.event.type') = ? AND r.detected_at >= ?)" for _ in keep_near)
        with self.transaction():
            self._x(f"""DELETE FROM beacons WHERE t < ? AND NOT EXISTS (
                            SELECT 1 FROM reports r
                            WHERE abs(json_extract(r.line, '$.event.latitude') - beacons.lat) <= ?
                              AND abs(json_extract(r.line, '$.event.longitude') - beacons.lon) <= ?
                              AND beacons.t >= r.detected_at - ? AND ({counted}))""",
                    (beacons_before, near_deg, near_deg, lead_s, *[v for item in keep_near.items() for v in item]))
            self._x("DELETE FROM hazard_evidence WHERE (device_id, event_id) IN "
                    "(SELECT device_id, event_id FROM reports WHERE detected_at < ?)", (reports_before,))
            self._x("DELETE FROM reports WHERE detected_at < ?", (reports_before,))
            self._x("DELETE FROM rejections WHERE received_at < ?", (rejections_before,))
            old = "(SELECT hazard_id FROM hazards WHERE state = 'expired' AND updated_at < ?)"
            for table in ("hazard_evidence", "hazard_passes", "reputation_events", "alert_deliveries"):
                self._x(f"DELETE FROM {table} WHERE hazard_id IN {old}", (hazards_before,))
            self._x("DELETE FROM hazards WHERE state = 'expired' AND updated_at < ?", (hazards_before,))
