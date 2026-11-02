"""Runs the trust engine in the background. Saving a report (the ACK) never waits for this; if the process stops,
pending reports are still there at the next start and nothing has been half counted, because each evaluation is
one database transaction. Also applies the retention rules.

Inside the API process by default (ICVSP_WORKER_INTERVAL_S), or on its own:  python -m icvsp_backend.worker"""
from __future__ import annotations

import logging
import os
import threading
import time

from .store_errors import StoreUnavailable

log = logging.getLogger("icvsp.worker")
PURGE_EVERY_S = 3600.0
RECHECK_EVERY_S = 60.0   # with nothing pending: hazards still age, and new positions count as evidence


class Worker(threading.Thread):
    def __init__(self, service, interval_s: float, clock=time.time):
        super().__init__(name="icvsp-evaluator", daemon=True)
        self.service, self.interval_s, self.clock = service, interval_s, clock
        self.stopping = threading.Event()
        self.next_purge = self.next_recheck = 0.0

    def run_once(self) -> None:
        now = self.clock()
        try:
            if self.service.store.pending_count() > 0 or now >= self.next_recheck:
                self.service.evaluate(now, settle=True)
                self.next_recheck = now + RECHECK_EVERY_S
            if now >= self.next_purge:
                self.service.purge(now)
                self.next_purge = now + PURGE_EVERY_S
        except StoreUnavailable as e:
            log.warning("database unavailable, will retry: %s", e)
        except Exception:
            log.exception("evaluation failed, reports stay pending")

    def run(self) -> None:
        while not self.stopping.wait(self.interval_s):
            self.run_once()

    def stop(self) -> None:
        self.stopping.set()


def main() -> None:
    from .app import store_from_env
    from .service import Service
    logging.basicConfig(level=logging.INFO)
    worker = Worker(Service(store_from_env()), float(os.environ.get("ICVSP_WORKER_INTERVAL_S", "10")))
    worker.run()


if __name__ == "__main__":
    main()
