"""Backend logic between the API and the trust engine: accept a report, evaluate pending reports, answer hazard
queries, and update device reputation once per confident outcome.

Saving and evaluating are two separate steps. `submit` answers as soon as the report is safely stored; `evaluate`
runs later (worker.py) in one database transaction, so a crash leaves reports pending and nothing half counted."""
from __future__ import annotations

import dataclasses
import hashlib
import json
import logging
import math
import secrets
import uuid

from icvsp.config import Config
from icvsp.consensus import Decision, Engine
from icvsp.geo import distance_m, heading_diff
from icvsp.model import Beacon, HazardState, from_iso
from icvsp.trust import Reputation
from icvsp.validation import schema_errors

from .ahead import AheadConfig, Route, straight_ahead
from .store import Store
from .store_errors import StoreUnavailable
from .verify import MAX_INT64, Envelope, Rejected, parse, verify_signature

log = logging.getLogger("icvsp.service")

BEACON_WINDOW_S = 2 * 3600   # position history kept in the decision (plausibility and "drove past")
FUTURE_TOLERANCE_S = 30.0    # device clocks are GNSS time, so a little more than the delivery and clock skew
# Reports are kept longer than the longest freshness window: a replay is then always either found as already
# seen or refused as too old, so the "seen" record can never forget something that would still be accepted.
REPORT_RETENTION_MARGIN_S = 24 * 3600
REJECTION_RETENTION_S = 90 * 24 * 3600
HAZARD_RETENTION_S = 30 * 24 * 3600
HAZARD_NAMESPACE = uuid.UUID("6f3a1c52-4b7e-5d10-9e21-0c8d7a4b1f33")


def _reasons(value) -> list:
    return json.loads(value) if isinstance(value, str) else value


def hash_code(code: str) -> str:
    return hashlib.sha256(code.encode()).hexdigest()


class Service:
    def __init__(self, store, cfg: Config | None = None, ahead: AheadConfig | None = None,
                 eval_on_read: bool | None = None):
        self.store, self.cfg, self.ahead_cfg = store, cfg or Config(), ahead or AheadConfig()
        # the simulation has no background worker and a simulated clock: decide when asked, as the engine always did
        self.eval_on_read = isinstance(store, Store) if eval_on_read is None else eval_on_read
        config = json.dumps(dataclasses.asdict(self.cfg), sort_keys=True)
        self.config_hash = hashlib.sha256(config.encode()).hexdigest()[:16]

    # ---------- devices ----------
    def register(self, device_id: str, public_key: str, now: float, vetted: bool = False) -> str:
        """Controlled provisioning (FR-SEC-04). `vetted` gives a known fleet device a starting history,
        as if it had 3 confirmed reports; ordinary devices start as new (reliability 0.25). Returns the one-time
        pairing code that lets the owner link the device to their account; only its hash is stored."""
        try:
            bytes.fromhex(public_key)
            if len(public_key) != 64:
                raise ValueError
        except ValueError:
            raise Rejected(400, "public_key must be 64 hex characters (Ed25519)", "bad_key") from None
        code = secrets.token_urlsafe(16)
        if not self.store.add_device(device_id, public_key.lower(), now, 3.0 if vetted else 0.0, hash_code(code)):
            raise Rejected(409, "device or key already registered", "already_registered")
        return code

    def new_pairing_code(self, device_id: str) -> str | None:
        code = secrets.token_urlsafe(16)
        return code if self.store.set_pairing_code(device_id, hash_code(code)) else None

    # ---------- reports ----------
    def submit(self, raw: bytes, now: float) -> dict:
        """Returns the ACK ({"status": "accepted" | "duplicate", ...}) after the report is durably stored, or raises
        Rejected (final, do not retry) or StoreUnavailable (nothing was stored, retry the same line later)."""
        env: Envelope | None = None
        try:
            env = parse(raw)
            dev = self.store.device(env.sender)
            if dev is None:
                raise Rejected(403, "unknown device", "unknown_device")
            if dev["status"] != "ACTIVE":
                raise Rejected(403, "device revoked", "device_revoked")
            verify_signature(env, dev["public_key"])
            ev = env.event
            errs = schema_errors(ev)
            if errs:
                raise Rejected(422, "schema: " + "; ".join(errs), "schema")
            if ev["boot_id"] > MAX_INT64 or ev["sequence"] > MAX_INT64:
                raise Rejected(422, "schema: boot_id or sequence out of range", "schema")
            if ev["device_id"] != env.sender or ev["event_id"] != env.msg_id:
                raise Rejected(422, "event device_id / event_id differ from the signed envelope", "envelope_mismatch")
            # An already stored report is answered before the time checks: when the ACK was lost and the phone
            # retries days later, it must still hear "you can delete it", not "too old".
            old = self.store.report(env.sender, ev["event_id"])
            if old is not None:
                return self._repeat(old, env)
            detected_at = from_iso(ev["detected_at"])
            sent_at = env.utc_ms / 1000
            self._check_time(ev["type"], detected_at, sent_at, now)
            if not self.store.add_report(ev, env.line, detected_at, sent_at, now):
                old = self.store.report(env.sender, ev["event_id"])   # another copy arrived in the same moment
                if old is not None:
                    return self._repeat(old, env)
                raise Rejected(409, f"repeated boot {ev['boot_id']} / sequence {ev['sequence']}", "counter_conflict")
            return {"status": "accepted", "ack": "stored", "event_id": ev["event_id"], "device_id": env.sender,
                    "stored_at": now, "evaluation": "pending"}
        except Rejected as r:
            try:
                self.store.reject(now, env.sender if env else None, env.msg_id if env else None, r.code, r.reason)
            except StoreUnavailable:
                log.warning("rejection not logged, database unavailable: %s", r.code)
            raise

    def _repeat(self, old, env: Envelope) -> dict:
        if old["line"] != env.line:
            raise Rejected(409, "event_id already used for a different report", "id_conflict")
        return {"status": "duplicate", "ack": "stored", "event_id": env.msg_id, "device_id": env.sender,
                "stored_at": old["received_at"], "evaluation": old["evaluation_status"]}

    def _check_time(self, hazard_type: str, detected_at: float, sent_at: float, now: float) -> None:
        """A report may be delivered late (no internet, device restarted, queue draining) and out of order, so age is
        judged against how long this hazard type stays meaningful, not against the last report seen."""
        for t in (detected_at, sent_at):
            if t > now + FUTURE_TOLERANCE_S:
                raise Rejected(422, "report time is in the future", "future")
            if t < now - self.cfg.max_age_s[hazard_type]:
                raise Rejected(422, f"report too old for a {hazard_type}", "stale")

    def add_beacons(self, beacons: list[dict]) -> int:
        rows = [(b["vehicle_id"], float(b["t"]), float(b["lat"]), float(b["lon"]), float(b["speed_mps"]),
                 float(b["heading_deg"]) % 360, int(bool(b.get("detector_on", True)))) for b in beacons]
        self.store.add_beacons(rows)
        return len(rows)

    def report_view(self, r) -> dict:
        ev = json.loads(r["line"])["event"]
        return {"event_id": r["event_id"], "device_id": r["device_id"], "type": ev["type"], "severity": ev["severity"],
                "latitude": ev["latitude"], "longitude": ev["longitude"], "heading_deg": ev["heading_deg"],
                "detection_confidence": ev["detection_confidence"], "detected_at": r["detected_at"],
                "received_at": r["received_at"], "evaluation": r["evaluation_status"],
                "evaluation_reasons": _reasons(r["evaluation_reasons"]), "settled": bool(r["settled"]),
                "hazard_ids": self.store.report_hazards(r["device_id"], r["event_id"])}

    # ---------- decisions ----------
    def _engine(self, now: float) -> tuple[Engine, list]:
        rep = Reputation(self.cfg.prior_good, self.cfg.prior_bad)
        for vid, (good, bad) in self.store.reputations().items():
            rep.seed(vid, good, bad)
        eng = Engine(self.cfg, rep)
        for b in self.store.beacons_since(now - BEACON_WINDOW_S):
            eng.ingest_beacon(Beacon(b["vehicle_id"], b["t"], b["lat"], b["lon"], b["speed_mps"],
                                     b["heading_deg"], bool(b["detector_on"])))
        rows = self.store.reports_since(self._oldest(now))
        for r in rows:
            eng.ingest_dict(json.loads(r["line"])["event"])
        return eng, rows

    def _oldest(self, now: float) -> float:
        return now - max(self.cfg.max_age_s.values())

    def decide(self, now: float) -> list[Decision]:
        eng, _ = self._engine(now)
        return eng.decide(now).decisions

    def evaluate(self, now: float, settle: bool = False) -> dict:
        """One trust-engine run over all current evidence: hazards, their evidence, each report's evaluation result
        and (with `settle`) reputation are written together or not at all. A second server finding the lock taken
        skips this round. The engine recalculates from the stored reports, so repeating a run counts nothing twice."""
        with self.store.transaction():
            if not self.store.try_evaluator_lock():
                return {"busy": True, "hazards": 0, "updates": []}
            pending = self.store.pending_count()
            eng, rows = self._engine(now)
            result = eng.decide(now)
            run_id = self.store.add_run(now, self.config_hash, len(rows), pending)
            ids = self._hazard_ids(result.decisions)
            for d, hazard_id in zip(result.decisions, ids):
                self._save_decision(d, hazard_id, run_id, now)
            self.store.expire_hazards_except(ids, now)
            refused = {(x.source, x.event_id): x.reasons for x in result.rejected}
            for r in rows:
                reasons = refused.get((r["device_id"], r["event_id"]))
                status, why = ("rejected_by_engine", reasons) if reasons is not None else ("evaluated", [])
                if r["evaluation_status"] != status or _reasons(r["evaluation_reasons"]) != why:
                    self.store.set_report_evaluation(r["device_id"], r["event_id"], status, why, now)
            self.store.close_pending_before(self._oldest(now), now)
            updates = self._settle(result.decisions, ids, now) if settle else []
            return {"busy": False, "run_id": run_id, "pending": pending, "hazards": len(ids), "updates": updates}

    def _hazard_ids(self, decisions: list[Decision]) -> list[str]:
        """The engine numbers its clusters anew on every run. A hazard keeps its ID if it shares evidence with a known
        hazard, or else sits at the same place. A new ID is derived from its first report, so the same inputs
        always give the same ID (NFR-07)."""
        free = {h["hazard_id"]: h for h in self.store.active_hazards()}
        ids: list[str | None] = [None] * len(decisions)
        keys = [{(e.source, e.event_id) for e in d.support} for d in decisions]
        for i, _ in sorted(enumerate(decisions), key=lambda p: p[1].first_seen):
            best = max(free.values(), key=lambda h: len(keys[i] & h["evidence"]), default=None)
            if best is not None and keys[i] & best["evidence"]:
                ids[i] = best["hazard_id"]
                del free[best["hazard_id"]]
        for i, d in enumerate(decisions):
            if ids[i] is None:
                near = [h for h in free.values() if h["type"] == d.type
                        and heading_diff(h["heading_deg"], d.heading_deg) <= self.cfg.heading_tol_deg
                        and distance_m(h["latitude"], h["longitude"], d.lat, d.lon) <= self.cfg.merge_radius_m]
                if near:
                    ids[i] = near[0]["hazard_id"]
                    del free[near[0]["hazard_id"]]
                else:
                    ids[i] = str(uuid.uuid5(HAZARD_NAMESPACE, f"{d.type}:{min(keys[i])}"))
        return ids

    def _expires_at(self, d: Decision, now: float) -> float:
        """When the engine will call this hazard expired if nothing new arrives (freshness below expire_freshness)."""
        if d.state == HazardState.EXPIRED:
            return now
        best = max(e.trust.f for e in d.support)
        tau = self.cfg.freshness_tau_s.get(d.type, 3600.0)
        return now + tau * math.log(best / self.cfg.expire_freshness) if best > 0 else now

    def _save_decision(self, d: Decision, hazard_id: str, run_id: int, now: float) -> None:
        def weights(e) -> dict:
            return {"c": e.trust.c, "g": e.trust.g, "h": e.trust.h, "f": e.trust.f, "weight": e.weight}
        self.store.save_hazard(
            {"hazard_id": hazard_id, "type": d.type, "state": d.state.value, "severity": d.severity,
             "latitude": d.lat, "longitude": d.lon, "heading_deg": d.heading_deg % 360, "first_seen": d.first_seen,
             "expires_at": self._expires_at(d, now), "independent_sources": d.independent_sources,
             "net_evidence": d.net, "sybil_suspected": d.sybil_suspected, "reasons": d.reasons,
             "last_run_id": run_id, "updated_at": now},
            [{"device_id": e.source, "event_id": e.event_id, **weights(e)} for e in d.support],
            list({e.source: {"device_id": e.source, **weights(e)} for e in d.contra}.values()))

    def _settle(self, decisions: list[Decision], ids: list[str], now: float) -> list[str]:
        """Reputation from confident outcomes: one device, one hazard, one outcome, however many runs see it."""
        log_lines = []
        for d, hazard_id in zip(decisions, ids):
            contra_groups = len({e.group for e in d.contra})
            confirmed = d.state == HazardState.CONFIRMED
            refuted = d.state == HazardState.SUPPRESSED and (contra_groups >= 2 or d.sybil_suspected)
            if not (confirmed or refuted):
                continue
            kind, good, bad, word = (("report_confirmed", 1.0, 0.0, "confirmed") if confirmed
                                     else ("report_contradicted", 0.0, 1.0, "contradicted"))
            for e in d.support:
                if self.store.add_reputation_event(e.source, hazard_id, kind, good, bad, now):
                    self.store.mark_settled(e.source, e.event_id)
                    log_lines.append(f"{e.source}: report {e.event_id} {word}")
            if confirmed:
                for e in d.contra:
                    if self.store.add_reputation_event(e.source, hazard_id, "missed_confirmed", 0.0, 0.5, now):
                        log_lines.append(f"{e.source}: missed confirmed {hazard_id}")
        return log_lines

    def settle(self, now: float) -> list[str]:
        return self.evaluate(now, settle=True)["updates"]

    def purge(self, now: float) -> None:
        """Retention (NFR-SEC-06): positions go first, reports only after replay protection no longer needs them."""
        self.store.purge(now - BEACON_WINDOW_S, self._oldest(now) - REPORT_RETENTION_MARGIN_S,
                         now - REJECTION_RETENTION_S, now - HAZARD_RETENTION_S)

    # ---------- hazard queries ----------
    def _view(self, h: dict, distance: float) -> dict:
        return {"hazard_id": h["hazard_id"], "type": h["type"], "state": h["state"],
                "level": "advisory" if h["state"] == HazardState.UNCONFIRMED.value else "warning",
                "severity": h["severity"], "latitude": round(h["latitude"], 6), "longitude": round(h["longitude"], 6),
                "heading_deg": round(h["heading_deg"], 1), "distance_m": round(distance, 1),
                "independent_sources": h["independent_sources"], "net_evidence": round(h["net_evidence"], 3),
                "reasons": h["reasons"], "expires_at": h["expires_at"]}

    def hazards(self, now: float, lat: float, lon: float, radius_m: float,
                heading: float | None = None) -> list[dict]:
        """Hazards worth showing near a position: advisory (unconfirmed) or warning (corroborated / confirmed).
        With a heading, only hazards in the same direction, still ahead and on this road are returned."""
        if self.eval_on_read:
            self.evaluate(now)
        states = (HazardState.UNCONFIRMED.value, HazardState.CORROBORATED.value, HazardState.CONFIRMED.value)
        out = []
        for h in self.store.nearby_hazards(lat, lon, radius_m, states, now):
            if heading is not None and straight_ahead(lat, lon, heading, h["latitude"], h["longitude"], h["heading_deg"],
                                                      self.cfg.heading_tol_deg, self.ahead_cfg) is None:
                continue
            out.append(self._view(h, h["distance_m"]))
        return out

    def hazards_ahead(self, now: float, user_id: str | None, lat: float, lon: float, heading: float | None,
                      speed_mps: float, route: list[tuple[float, float]] | None, advisories: bool = False) -> dict:
        """What the app needs while driving. The position is used for this answer and not stored."""
        if self.eval_on_read:
            self.evaluate(now)
        cfg = self.ahead_cfg
        states = (HazardState.CORROBORATED.value, HazardState.CONFIRMED.value)
        if advisories:
            states += (HazardState.UNCONFIRMED.value,)
        planned = Route(lat, lon, route, cfg) if route else None
        use_route = planned is not None and planned.usable
        alert_m = max(cfg.alert_distance_m, speed_mps * cfg.alert_lead_s)
        out = []
        for h in self.store.nearby_hazards(lat, lon, cfg.search_radius_m, states, now):
            tol = self.cfg.heading_tol_deg
            if use_route:
                dist = planned.distance_to(h["latitude"], h["longitude"], h["heading_deg"], tol)
            elif heading is not None:
                dist = straight_ahead(lat, lon, heading, h["latitude"], h["longitude"], h["heading_deg"], tol, cfg)
            else:
                dist = None
            if dist is None:
                continue
            view = self._view(h, dist)
            view["distance_basis"] = "route" if use_route else "straight_line"
            view["in_alert_range"] = dist <= alert_m
            view["alert"] = (view["in_alert_range"] and view["level"] == "warning"
                             and (user_id is None or self.store.claim_alert(user_id, h["hazard_id"], now,
                                                                            cfg.alert_cooldown_s)))
            out.append(view)
        return {"hazards": sorted(out, key=lambda v: v["distance_m"]), "search_radius_m": cfg.search_radius_m,
                "alert_distance_m": alert_m,
                "route_status": "none" if planned is None else "followed" if use_route else "off_route"}

    # ---------- operator view ----------
    def overview(self, now: float, recent: int = 30, position_window_s: float = 15.0) -> dict:
        """Everything the operator dashboard shows: devices with their reliability, every current hazard
        decision (including suppressed ones and why), live vehicle positions, latest reports and rejections."""
        eng, _ = self._engine(now)
        result = eng.decide(now)
        rep = Reputation(self.cfg.prior_good, self.cfg.prior_bad)
        devices = []
        for d in self.store.devices():
            rep.seed(d["device_id"], d["good"], d["bad"])
            devices.append({"device_id": d["device_id"], "status": d["status"], "good": d["good"], "bad": d["bad"],
                            "reliability": round(rep.score(d["device_id"]), 3)})
        hazards = [{
            "hazard_id": d.hazard_id, "type": d.type, "state": d.state.value, "action": d.action,
            "severity": d.severity,
            "latitude": round(d.lat, 6), "longitude": round(d.lon, 6), "heading_deg": round(d.heading_deg, 1),
            "independent_sources": d.independent_sources, "net_evidence": round(d.net, 3),
            "support": sorted({e.source for e in d.support}), "contra": len({e.source for e in d.contra}),
            "sybil_suspected": d.sybil_suspected, "reasons": d.reasons,
        } for d in result.decisions]
        reports = []
        for r in self.store.recent_reports(recent):
            ev = json.loads(r["line"])["event"]
            reports.append({"event_id": r["event_id"], "device_id": r["device_id"], "type": ev["type"],
                            "latitude": ev["latitude"], "longitude": ev["longitude"],
                            "detection_confidence": ev["detection_confidence"], "detected_at": r["detected_at"],
                            "received_at": r["received_at"], "settled": bool(r["settled"]),
                            "evaluation": r["evaluation_status"]})
        return {
            "now": now,
            "counts": {k: self.store.count(k) for k in ("devices", "reports", "rejections")},
            "devices": devices, "hazards": hazards, "reports": reports,
            "rejections": [dict(r) for r in self.store.rejections(recent)][::-1],
            "engine_rejected": [{"event_id": x.event_id, "device_id": x.source, "reasons": x.reasons}
                                for x in result.rejected[-recent:]],
            "vehicles": [{"vehicle_id": b["vehicle_id"], "t": b["t"], "lat": b["lat"], "lon": b["lon"],
                          "heading_deg": b["heading_deg"], "speed_mps": b["speed_mps"]}
                         for b in self.store.latest_positions(now - position_window_s, now)],
        }
