"""How long one evaluator run and one cleanup take as traffic grows, on a throwaway Supabase PostgreSQL. It wipes the
public schema, like test_postgres.py. The traffic is synthetic: cars on straight roads, so this shows how the cost
grows, not what a real city will do.

Run:
    docker run -d --name icvsp-pg -e POSTGRES_PASSWORD=postgres -p 54329:5432 supabase/postgres:17.11.0.003
    ICVSP_TEST_DATABASE_URL=postgresql://postgres:postgres@127.0.0.1:54329/postgres python verification/measure_evaluator.py
"""
import math
import random
import signal
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "backend"))
import icvsp_backend  # noqa: F401,E402  (puts the engine on the import path)
from icvsp.geo import offset  # noqa: E402
from icvsp_backend.pg_store import PgStore  # noqa: E402
from icvsp_backend.service import Service  # noqa: E402
from tests.test_api import Car  # noqa: E402
from tests.test_postgres import TABLES, admin_connection, backend_url, setUpModule as prepare_database  # noqa: E402

LAT, LON, T0 = 30.0444, 31.2357, 1790000000.0
HOUR, SPEED = 3600, 11.0
LEVELS = [(20, 200, 1800), (100, 500, 1800), (300, 1000, 1800)]   # cars, reports, seconds each car drove
LIMIT_S = 240


def beacons(rng: random.Random, car: Car, start: float, seconds: int) -> list[dict]:
    north, east, heading = rng.uniform(-5000, 5000), rng.uniform(-5000, 5000), rng.choice((0.0, 90.0, 180.0, 270.0))
    rad = math.radians(heading)
    rows = []
    for k in range(seconds):
        lat, lon = offset(LAT, LON, north + SPEED * k * math.cos(rad), east + SPEED * k * math.sin(rad))
        rows.append({"vehicle_id": car.id, "t": start + k, "lat": lat, "lon": lon, "speed_mps": SPEED,
                     "heading_deg": heading})
    return rows


def timed(what: str, action) -> float:
    def give_up(*_):
        raise TimeoutError
    signal.signal(signal.SIGALRM, give_up)
    signal.alarm(LIMIT_S)
    start = time.perf_counter()
    try:
        action()
    except TimeoutError:
        print(f"  {what}: did not finish in {LIMIT_S} s", flush=True)
        return math.inf
    finally:
        signal.alarm(0)
    taken = time.perf_counter() - start
    print(f"  {what}: {taken:.2f} s", flush=True)
    return taken


def measure(cars_n: int, reports_n: int, seconds: int) -> None:
    print(f"{cars_n} cars, {reports_n} reports, {cars_n * seconds} positions", flush=True)
    with admin_connection() as db:
        db.execute(f"truncate {TABLES} cascade")
    store = PgStore(backend_url())
    svc = Service(store)
    rng = random.Random(1)
    now = T0 + seconds + 60
    cars = [Car(f"car-{i}") for i in range(cars_n)]
    drives = {}
    for car in cars:
        svc.register(car.id, car.public_hex, T0 - HOUR, vetted=True)
        drives[car.id] = beacons(rng, car, T0, seconds)
        svc.add_beacons(drives[car.id])
    sites = [(row["lat"], row["lon"], row["heading_deg"]) for row in
             (rng.choice(drives[c.id]) for c in rng.choices(cars, k=max(1, reports_n // 4)))]
    lines = []
    for i in range(reports_n):
        car = cars[i % cars_n]
        lat, lon, heading = rng.choice(sites)
        lines.append(car.report(T0 + rng.uniform(0, seconds), lat, lon, heading=heading))
    start = time.perf_counter()
    for line in lines:
        svc.submit(line, now)
    print(f"  store one report (ACK), mean: {(time.perf_counter() - start) / reports_n * 1000:.1f} ms", flush=True)
    timed("evaluate", lambda: svc.evaluate(now))
    timed("cleanup, 3 h later", lambda: svc.purge(now + 3 * HOUR))
    print(f"  positions kept after cleanup: {store.count('beacons')}", flush=True)
    timed("evaluate, 3 h later", lambda: svc.evaluate(now + 3 * HOUR))
    store.close()


if __name__ == "__main__":
    prepare_database()
    for level in LEVELS:
        measure(*level)
