"""Storage on Supabase (PostgreSQL + PostGIS). Same methods as store.Store; the schema, RLS and PostGIS search are
in database/supabase/migrations. Connects as the restricted role icvsp_backend, which passes RLS, so user access
is checked in the API before these methods run (app.py)."""
from __future__ import annotations

import threading
from contextlib import contextmanager

import psycopg
from psycopg.rows import dict_row
from psycopg.types.json import Jsonb
from psycopg_pool import ConnectionPool, PoolTimeout

from .store_errors import StoreUnavailable

EVALUATOR_LOCK = 727_001

REPORT_COLUMNS = """device_id, event_id, boot_id, sequence, line, settled, evaluation_status, evaluation_reasons,
    extract(epoch from detected_at)::float8 as detected_at, extract(epoch from received_at)::float8 as received_at,
    extract(epoch from evaluated_at)::float8 as evaluated_at"""
HAZARD_COLUMNS = """hazard_id::text as hazard_id, type, state, severity, latitude, longitude, heading_deg,
    independent_sources, net_evidence, sybil_suspected, reasons, last_run_id,
    extract(epoch from first_seen)::float8 as first_seen, extract(epoch from expires_at)::float8 as expires_at,
    extract(epoch from updated_at)::float8 as updated_at"""
BEACON_COLUMNS = """vehicle_id, extract(epoch from t)::float8 as t, lat, lon, speed_mps, heading_deg, detector_on"""


class PgStore:
    def __init__(self, url: str, max_connections: int = 10, timeout_s: float = 5.0):
        # prepare_threshold=None: Supabase's transaction-mode pooler cannot keep prepared statements
        self.pool = ConnectionPool(url, min_size=1, max_size=max_connections, timeout=timeout_s, open=True,
                                   kwargs={"autocommit": True, "row_factory": dict_row, "prepare_threshold": None})
        self.local = threading.local()

    def close(self) -> None:
        self.pool.close()

    def ping(self) -> None:
        self._x("select 1")

    @contextmanager
    def transaction(self):
        """Everything inside commits together or not at all. Nested use joins the outer transaction."""
        if getattr(self.local, "conn", None) is not None:
            yield
            return
        try:
            with self.pool.connection() as conn, conn.transaction():
                self.local.conn = conn
                try:
                    yield
                finally:
                    self.local.conn = None
        except (psycopg.OperationalError, PoolTimeout) as e:
            raise StoreUnavailable(str(e)) from e

    def try_evaluator_lock(self) -> bool:
        """Only one evaluator at a time, across all server processes; released when the transaction ends."""
        return self._x("select pg_try_advisory_xact_lock(%s) as got", (EVALUATOR_LOCK,)).fetchone()["got"]

    def _x(self, sql: str, args=None):
        conn = getattr(self.local, "conn", None)
        if conn is not None:
            return conn.execute(sql, args)
        try:
            with self.pool.connection() as conn:
                return conn.execute(sql, args)
        except (psycopg.OperationalError, PoolTimeout) as e:
            raise StoreUnavailable(str(e)) from e

    # devices
    def add_device(self, device_id: str, public_key: str, now: float, good: float = 0.0,
                   pairing_code_hash: str | None = None) -> bool:
        return self._x("""insert into public.devices (device_id, public_key, registered_at, good, pairing_code_hash)
                          values (%s, %s, to_timestamp(%s), %s, %s) on conflict do nothing""",
                       (device_id, public_key, now, good, pairing_code_hash)).rowcount == 1

    def device(self, device_id: str) -> dict | None:
        return self._x("select * from public.devices where device_id = %s", (device_id,)).fetchone()

    def set_status(self, device_id: str, status: str) -> bool:
        return self._x("update public.devices set status = %s where device_id = %s",
                       (status, device_id)).rowcount == 1

    def set_pairing_code(self, device_id: str, code_hash: str) -> bool:
        return self._x("update public.devices set pairing_code_hash = %s where device_id = %s",
                       (code_hash, device_id)).rowcount == 1

    def reputations(self) -> dict[str, tuple[float, float]]:
        return {r["device_id"]: (r["good"], r["bad"])
                for r in self._x("select device_id, good, bad from public.devices").fetchall()}

    def devices(self) -> list[dict]:
        return self._x("""select device_id, status, good, bad, extract(epoch from registered_at)::float8 as registered
                          from public.devices order by device_id""").fetchall()

    def add_reputation_event(self, device_id: str, hazard_id: str, kind: str, good: float, bad: float,
                             now: float) -> bool:
        with self.transaction():
            cur = self._x("""insert into public.reputation_events values (%s, %s, %s, %s, %s, to_timestamp(%s))
                             on conflict do nothing""", (device_id, hazard_id, kind, good, bad, now))
            if cur.rowcount != 1:
                return False
            self._x("update public.devices set good = good + %s, bad = bad + %s where device_id = %s",
                    (good, bad, device_id))
            return True

    def counters(self, device_id: str) -> dict | None:
        return self._x("select * from public.device_counters where device_id = %s", (device_id,)).fetchone()

    # reports
    def report(self, device_id: str, event_id: str) -> dict | None:
        return self._x(f"select {REPORT_COLUMNS} from public.reports where device_id = %s and event_id = %s",
                       (device_id, event_id)).fetchone()

    def add_report(self, ev: dict, line: str, detected_at: float, sent_at: float, now: float) -> bool:
        """A concurrent copy waits here for the first one to commit, then finds the row and inserts nothing."""
        with self.transaction():
            cur = self._x("""insert into public.reports (device_id, event_id, boot_id, sequence, type, severity,
                               detection_confidence, gps_valid, latitude, longitude, position_accuracy_m, speed_kmh,
                               heading_deg, imu_peak_g, detected_at, sent_at, received_at, line)
                             values (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s,
                                     to_timestamp(%s), to_timestamp(%s), to_timestamp(%s), %s)
                             on conflict do nothing""",
                          (ev["device_id"], ev["event_id"], ev["boot_id"], ev["sequence"], ev["type"], ev["severity"],
                           ev["detection_confidence"], ev["gps_valid"], ev["latitude"], ev["longitude"],
                           ev["position_accuracy_m"], ev["speed_kmh"], ev["heading_deg"], ev.get("imu_peak_g"),
                           detected_at, sent_at, now, line))
            if cur.rowcount != 1:
                return False
            self._x("""insert into public.device_counters as c values (%s, %s, %s, 1, to_timestamp(%s))
                       on conflict (device_id) do update set
                         highest_sequence = case
                           when excluded.highest_boot_id > c.highest_boot_id then excluded.highest_sequence
                           when excluded.highest_boot_id = c.highest_boot_id
                             then greatest(c.highest_sequence, excluded.highest_sequence)
                           else c.highest_sequence end,
                         highest_boot_id = greatest(c.highest_boot_id, excluded.highest_boot_id),
                         reports_stored = c.reports_stored + 1, last_report_at = excluded.last_report_at""",
                    (ev["device_id"], ev["boot_id"], ev["sequence"], now))
            return True

    def reports_since(self, t: float) -> list[dict]:
        return self._x(f"select {REPORT_COLUMNS} from public.reports where detected_at >= to_timestamp(%s) "
                       "order by detected_at", (t,)).fetchall()

    def device_reports(self, device_id: str, limit: int) -> list[dict]:
        return self._x(f"select {REPORT_COLUMNS} from public.reports where device_id = %s "
                       "order by detected_at desc limit %s", (device_id, limit)).fetchall()

    def report_hazards(self, device_id: str, event_id: str) -> list[str]:
        return [r["hazard_id"] for r in self._x(
            "select hazard_id::text as hazard_id from public.hazard_evidence where device_id = %s and event_id = %s",
            (device_id, event_id)).fetchall()]

    def set_report_evaluation(self, device_id: str, event_id: str, status: str, reasons: list[str], now: float) -> None:
        self._x("""update public.reports set evaluation_status = %s, evaluation_reasons = %s,
                   evaluated_at = to_timestamp(%s) where device_id = %s and event_id = %s""",
                (status, Jsonb(reasons), now, device_id, event_id))

    def close_pending_before(self, t: float, now: float) -> None:
        self._x("""update public.reports set evaluation_status = 'evaluated', evaluated_at = to_timestamp(%s),
                   evaluation_reasons = '["aged out before evaluation"]'
                   where evaluation_status = 'pending' and detected_at < to_timestamp(%s)""", (now, t))

    def pending_count(self) -> int:
        return self._x("select count(*) as n from public.reports where evaluation_status = 'pending'").fetchone()["n"]

    def mark_settled(self, device_id: str, event_id: str) -> None:
        self._x("update public.reports set settled = true where device_id = %s and event_id = %s",
                (device_id, event_id))

    def reject(self, now: float, device_id: str | None, event_id: str | None, code: str, reason: str) -> None:
        self._x("""insert into public.rejections (received_at, device_id, event_id, code, reason)
                   values (to_timestamp(%s), %s, %s, %s, %s)""", (now, device_id, event_id, code, reason))

    def rejections(self, limit: int = -1) -> list[dict]:
        rows = self._x("""select extract(epoch from received_at)::float8 as received_at, device_id, event_id, code, reason
                          from public.rejections order by received_at desc, rejection_id desc
                          limit %s""", (None if limit < 0 else limit,)).fetchall()
        return rows[::-1]

    def recent_reports(self, limit: int) -> list[dict]:
        return self._x(f"select {REPORT_COLUMNS} from public.reports order by received_at desc limit %s",
                       (limit,)).fetchall()

    def count(self, table: str) -> int:
        assert table in ("devices", "reports", "rejections", "beacons", "hazards")
        return self._x(f"select count(*) as n from public.{table}").fetchone()["n"]

    # beacons
    def add_beacons(self, rows: list[tuple]) -> None:
        """Positions of unregistered vehicles are dropped by the join."""
        if not rows:
            return
        columns = [list(c) for c in zip(*rows)]
        self._x("""insert into public.beacons
                   select v.id, to_timestamp(v.t), v.lat, v.lon, v.speed, v.heading, v.det::int::boolean
                   from unnest(%s::text[], %s::float8[], %s::float8[], %s::float8[], %s::float8[], %s::float8[], %s::int[])
                        as v(id, t, lat, lon, speed, heading, det)
                   join public.devices d on d.device_id = v.id
                   on conflict do nothing""", columns)

    def beacons_since(self, t: float) -> list[dict]:
        return self._x(f"select {BEACON_COLUMNS} from public.beacons where t >= to_timestamp(%s) order by t",
                       (t,)).fetchall()

    def latest_positions(self, since: float, until: float) -> list[dict]:
        return self._x(f"""select distinct on (vehicle_id) {BEACON_COLUMNS} from public.beacons
                           where t between to_timestamp(%s) and to_timestamp(%s)
                           order by vehicle_id, beacons.t desc""", (since, until)).fetchall()

    # trust engine results
    def add_run(self, now: float, config_hash: str, reports: int, pending: int) -> int:
        return self._x("""insert into public.evaluation_runs (ran_at, config_hash, reports, pending)
                          values (to_timestamp(%s), %s, %s, %s) returning run_id""",
                       (now, config_hash, reports, pending)).fetchone()["run_id"]

    def active_hazards(self) -> list[dict]:
        out = {r["hazard_id"]: {**r, "evidence": set()} for r in self._x(
            f"select {HAZARD_COLUMNS} from public.hazards where state <> 'expired' order by first_seen, hazard_id"
        ).fetchall()}
        for r in self._x("""select e.hazard_id::text as hazard_id, e.device_id, e.event_id from public.hazard_evidence e
                            join public.hazards h using (hazard_id) where h.state <> 'expired'""").fetchall():
            if r["hazard_id"] in out:
                out[r["hazard_id"]]["evidence"].add((r["device_id"], r["event_id"]))
        return list(out.values())

    def save_hazard(self, h: dict, support: list[dict], passes: list[dict]) -> None:
        with self.transaction():
            self._x("""insert into public.hazards as o (hazard_id, type, state, severity, latitude, longitude, heading_deg,
                         first_seen, expires_at, independent_sources, net_evidence, sybil_suspected, reasons,
                         last_run_id, updated_at)
                       values (%(hazard_id)s, %(type)s, %(state)s, %(severity)s, %(latitude)s, %(longitude)s,
                         %(heading_deg)s, to_timestamp(%(first_seen)s), to_timestamp(%(expires_at)s),
                         %(independent_sources)s, %(net_evidence)s, %(sybil_suspected)s, %(reasons_json)s,
                         %(last_run_id)s, to_timestamp(%(updated_at)s))
                       on conflict (hazard_id) do update set state = excluded.state, severity = excluded.severity,
                         latitude = excluded.latitude, longitude = excluded.longitude,
                         heading_deg = excluded.heading_deg, first_seen = least(o.first_seen, excluded.first_seen),
                         expires_at = excluded.expires_at, independent_sources = excluded.independent_sources,
                         net_evidence = excluded.net_evidence, sybil_suspected = excluded.sybil_suspected,
                         reasons = excluded.reasons, last_run_id = excluded.last_run_id,
                         updated_at = excluded.updated_at""", {**h, "reasons_json": Jsonb(h["reasons"])})
            self._x("delete from public.hazard_evidence where hazard_id = %s", (h["hazard_id"],))
            self._x("delete from public.hazard_passes where hazard_id = %s", (h["hazard_id"],))
            for e in support:
                self._x("""insert into public.hazard_evidence
                           values (%(hazard_id)s, %(device_id)s, %(event_id)s, %(c)s, %(g)s, %(h)s, %(f)s, %(weight)s)""",
                        {**e, "hazard_id": h["hazard_id"]})
            for e in passes:
                self._x("""insert into public.hazard_passes
                           values (%(hazard_id)s, %(device_id)s, %(c)s, %(g)s, %(h)s, %(f)s, %(weight)s)""",
                        {**e, "hazard_id": h["hazard_id"]})

    def expire_hazards_except(self, keep: list[str], now: float) -> None:
        self._x("""update public.hazards set state = 'expired', expires_at = to_timestamp(%s), updated_at = to_timestamp(%s)
                   where state <> 'expired' and hazard_id <> all (%s::uuid[])""", (now, now, keep))

    def hazard(self, hazard_id: str) -> dict | None:
        return self._x(f"select {HAZARD_COLUMNS} from public.hazards where hazard_id = %s", (hazard_id,)).fetchone()

    def hazard_evidence(self, hazard_id: str) -> dict:
        return {"support": self._x("select device_id, event_id, c, g, h, f, weight from public.hazard_evidence "
                                   "where hazard_id = %s", (hazard_id,)).fetchall(),
                "passes": self._x("select device_id, c, g, h, f, weight from public.hazard_passes where hazard_id = %s",
                                  (hazard_id,)).fetchall()}

    def nearby_hazards(self, lat: float, lon: float, radius_m: float, states: tuple[str, ...], now: float) -> list[dict]:
        return self._x("""select hazard_id::text as hazard_id, type, state, severity, latitude, longitude, heading_deg,
                            extract(epoch from first_seen)::float8 as first_seen,
                            extract(epoch from expires_at)::float8 as expires_at,
                            independent_sources, net_evidence, sybil_suspected, reasons, distance_m
                          from public.nearby_hazards(%s, %s, %s, %s, to_timestamp(%s))""",
                       (lat, lon, radius_m, list(states), now)).fetchall()

    # users (profiles are created by a trigger on auth.users)
    def profile(self, user_id: str) -> dict | None:
        return self._x("select user_id::text as user_id, role from public.profiles where user_id = %s",
                       (user_id,)).fetchone()

    def set_role(self, user_id: str, role: str) -> bool:
        return self._x("update public.profiles set role = %s where user_id = %s", (role, user_id)).rowcount == 1

    def user_devices(self, user_id: str) -> list[str]:
        return [r["device_id"] for r in self._x(
            "select device_id from public.user_devices where user_id = %s order by device_id", (user_id,)).fetchall()]

    def owns_device(self, user_id: str, device_id: str) -> bool:
        return self._x("select 1 from public.user_devices where user_id = %s and device_id = %s",
                       (user_id, device_id)).fetchone() is not None

    def owned_among(self, user_id: str, device_ids: list[str]) -> set[str]:
        return {r["device_id"] for r in self._x(
            "select device_id from public.user_devices where user_id = %s and device_id = any(%s)",
            (user_id, device_ids)).fetchall()}

    def claim_device(self, user_id: str, device_id: str, code_hash: str, now: float) -> bool:
        with self.transaction():
            cur = self._x("""update public.devices set pairing_code_hash = null
                             where device_id = %s and pairing_code_hash = %s and status = 'ACTIVE'""",
                          (device_id, code_hash))
            if cur.rowcount != 1:
                return False
            self._x("""insert into public.user_devices values (%s, %s, to_timestamp(%s)) on conflict do nothing""",
                    (user_id, device_id, now))
            return True

    def claim_alert(self, user_id: str, hazard_id: str, now: float, cooldown_s: float) -> bool:
        return self._x("""insert into public.alert_deliveries as a values (%s, %s, to_timestamp(%s))
                          on conflict (user_id, hazard_id) do update set last_alerted_at = excluded.last_alerted_at
                          where a.last_alerted_at <= to_timestamp(%s)""",
                       (user_id, hazard_id, now, now - cooldown_s)).rowcount == 1

    # retention
    def purge(self, beacons_before: float, keep_near: dict[str, float], near_deg: float, lead_s: float,
              reports_before: float, rejections_before: float, hazards_before: float) -> None:
        counted = " or ".join("(r.type = %s and r.detected_at >= to_timestamp(%s))" for _ in keep_near)
        with self.transaction():
            self._x(f"""delete from public.beacons b where b.t < to_timestamp(%s) and not exists (
                            select 1 from public.reports r
                            where abs(r.latitude - b.lat) <= %s and abs(r.longitude - b.lon) <= %s
                              and b.t >= r.detected_at - make_interval(secs => %s) and ({counted}))""",
                    (beacons_before, near_deg, near_deg, lead_s, *[v for item in keep_near.items() for v in item]))
            self._x("delete from public.reports where detected_at < to_timestamp(%s)", (reports_before,))
            self._x("delete from public.rejections where received_at < to_timestamp(%s)", (rejections_before,))
            self._x("delete from public.hazards where state = 'expired' and updated_at < to_timestamp(%s)",
                    (hazards_before,))
            self._x("delete from public.evaluation_runs r where ran_at < to_timestamp(%s) and not exists "
                    "(select 1 from public.hazards h where h.last_run_id = r.run_id)", (hazards_before,))
