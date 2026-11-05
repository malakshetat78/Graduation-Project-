"""Rate limits (FR-SEC-20). In memory, so they are per process: with more than one server process each process counts
on its own, and this module has to be replaced by a shared counter (Redis, or a table in the database).

Who is counted:
* endpoints without a token (`POST /reports`, `/health`, `/ready`, the dashboard and the API docs): by client IP;
* endpoints with a token: by user ID, so many people behind one mobile-network IP do not block each other;
* failed sign-ins (wrong admin token, invalid bearer token): by client IP.
Nothing is counted by device ID before the signature has been checked, or anyone could use up another device's limit.

The client IP is the address of the TCP connection unless ICVSP_TRUST_PROXY_HEADERS=1. Behind Railway or Render
every request comes from the platform's proxy, so then X-Forwarded-For is needed; it is off by default because any
client can send that header and pick its own "IP". Turn it on only when the server can be reached through the proxy
alone. The proxy appends the address it saw, so the entry counted from the right (ICVSP_TRUSTED_PROXY_HOPS, default 1)
is the trustworthy one; entries further left are whatever the client wrote."""
from __future__ import annotations

import ipaddress
import logging
import math
import os
import threading
import time
from collections import OrderedDict, deque
from dataclasses import dataclass
from typing import Callable

from fastapi import Request

log = logging.getLogger("icvsp.security")

# name -> (requests, per seconds). Override one with e.g. ICVSP_RATE_REPORTS=300/60.
DEFAULTS: dict[str, tuple[int, int]] = {
    "reports": (600, 60),        # POST /reports, per IP. Wide: many phones can share one mobile-network IP
    "health": (120, 60),         # per IP. Platform health checks are far below this
    "ready": (30, 60),           # per IP. Every call queries the database
    "pages": (60, 60),           # /dashboard, /docs, /redoc, /openapi.json, per IP
    "user": (300, 60),           # all calls of one signed-in user. A phone sends positions about once a second
    "claim": (10, 60),           # POST /me/devices per user: guesses of a pairing code
    "bad_token": (30, 60),       # invalid bearer tokens per IP; over this, the 401 becomes a 429
    "bad_admin_token": (10, 60), # wrong X-Admin-Token per IP; over this, the token is not even compared
}
MAX_KEYS = 50_000


@dataclass(frozen=True)
class Decision:
    allowed: bool
    retry_after: int = 0
    first_block: bool = False     # the first refusal in this window: the only one worth a log line


class RateLimited(Exception):
    def __init__(self, retry_after: int):
        super().__init__("rate limited")
        self.retry_after = retry_after


class Limiter:
    """Sliding window: at most `limit` hits in any `window` seconds, counted per key."""

    def __init__(self, limit: int, window: float, clock: Callable[[], float] = time.monotonic, max_keys: int = MAX_KEYS):
        self.limit, self.window, self.clock, self.max_keys = limit, window, clock, max_keys
        self._hits: OrderedDict[str, deque] = OrderedDict()
        self._blocked: set[str] = set()
        self._lock = threading.Lock()

    def _trim(self, key: str, now: float) -> deque:
        q = self._hits.get(key)
        if q is None:
            q = self._hits[key] = deque()
            if len(self._hits) > self.max_keys:
                self._purge(now)
        while q and q[0] <= now - self.window:
            q.popleft()
        if not q:
            self._blocked.discard(key)
        return q

    def _purge(self, now: float) -> None:
        for k in [k for k, q in self._hits.items() if not q or q[-1] <= now - self.window]:
            del self._hits[k]
            self._blocked.discard(k)
        while len(self._hits) > self.max_keys:          # still full: forget the least recently added keys
            k, _ = self._hits.popitem(last=False)
            self._blocked.discard(k)

    def hit(self, key: str) -> Decision:
        """Counts one request if it is allowed. A refused request is not counted, so waiting is enough."""
        with self._lock:
            now = self.clock()
            q = self._trim(key, now)
            if len(q) >= self.limit:
                first = key not in self._blocked
                self._blocked.add(key)
                return Decision(False, max(1, math.ceil(q[0] + self.window - now)), first)
            q.append(now)
            return Decision(True)

    def blocked(self, key: str) -> Decision:
        """Like hit(), without counting."""
        with self._lock:
            now = self.clock()
            q = self._trim(key, now)
            if len(q) >= self.limit:
                first = key not in self._blocked
                self._blocked.add(key)
                return Decision(False, max(1, math.ceil(q[0] + self.window - now)), first)
            return Decision(True)


class RateLimits:
    def __init__(self, enabled: bool = True, trust_proxy: bool = False, proxy_hops: int = 1,
                 rules: dict[str, tuple[int, int]] | None = None, clock: Callable[[], float] = time.monotonic):
        self.enabled, self.trust_proxy, self.proxy_hops = enabled, trust_proxy, max(1, proxy_hops)
        self.rules = {**DEFAULTS, **(rules or {})}
        self.limiters = {name: Limiter(n, w, clock) for name, (n, w) in self.rules.items()}

    @classmethod
    def from_env(cls, clock: Callable[[], float] = time.monotonic, default_enabled: bool = True) -> "RateLimits":
        env = os.environ
        rules = {}
        for name in DEFAULTS:
            raw = env.get(f"ICVSP_RATE_{name.upper()}")
            if raw:
                try:
                    n, w = raw.split("/")
                    rules[name] = (int(n), int(w))
                    if rules[name][0] < 1 or rules[name][1] < 1:
                        raise ValueError
                except ValueError:
                    raise RuntimeError(f"ICVSP_RATE_{name.upper()} must look like 300/60 (requests/seconds)") from None
        try:
            hops = int(env.get("ICVSP_TRUSTED_PROXY_HOPS", "1"))
        except ValueError:
            raise RuntimeError("ICVSP_TRUSTED_PROXY_HOPS must be a whole number") from None
        return cls(enabled=env.get("ICVSP_RATE_LIMIT_ENABLED", "1" if default_enabled else "0") != "0",
                   trust_proxy=env.get("ICVSP_TRUST_PROXY_HEADERS") == "1", proxy_hops=hops, rules=rules, clock=clock)

    def client_ip(self, request: Request) -> str:
        peer = request.client.host if request.client else "unknown"
        if not self.trust_proxy:
            return peer                                     # a forwarded header is ignored, whatever it says
        parts = [p.strip() for p in request.headers.get("x-forwarded-for", "").split(",") if p.strip()]
        if len(parts) < self.proxy_hops:
            return peer                                     # no proxy in front (e.g. a laptop demo)
        try:
            return str(ipaddress.ip_address(parts[-self.proxy_hops]))
        except ValueError:
            return peer

    def check(self, rule: str, key: str) -> None:
        """Raises RateLimited when `key` is over the limit of `rule`."""
        if not self.enabled:
            return
        d = self.limiters[rule].hit(key)
        if not d.allowed:
            self._refused(rule, key, d)

    def peek(self, rule: str, key: str) -> None:
        """Raises RateLimited when `key` is already over the limit, without counting this call."""
        if not self.enabled:
            return
        d = self.limiters[rule].blocked(key)
        if not d.allowed:
            self._refused(rule, key, d)

    def failure(self, rule: str, key: str) -> int:
        """Counts one failure. Returns 0, or the seconds to wait when the key is now over the limit."""
        if not self.enabled:
            return 0
        d = self.limiters[rule].hit(key)
        if d.allowed:
            return 0
        self._log(rule, key, d)
        return d.retry_after

    def _refused(self, rule: str, key: str, d: Decision) -> None:
        self._log(rule, key, d)
        raise RateLimited(d.retry_after)

    def _log(self, rule: str, key: str, d: Decision) -> None:
        if d.first_block:       # one line per key and window, so an attack cannot flood the log as well
            n, w = self.rules[rule]
            log.warning("rate_limited rule=%s limit=%d/%ds key=%s retry_after=%ds", rule, n, w, key, d.retry_after)
