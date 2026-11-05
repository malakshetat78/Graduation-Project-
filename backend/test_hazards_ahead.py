"""Terminal client for the existing location-based FastAPI hazard endpoints.

Run the FastAPI server separately, then:
    python test_hazards_ahead.py
or:
    python test_hazards_ahead.py --lat 30.0444 --lon 31.2357\n    python test_hazards_ahead.py --lat 30.0444 --lon 31.2357 --heading 90 --speed 12

This sends only location/direction to FastAPI; FastAPI queries its configured
store (Supabase PostgreSQL when DATABASE_URL is set). No DB credentials here.
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode\nfrom urllib.request import Request, urlopen


def number_or_prompt(value: float | None, label: str, *, minimum: float,
                     maximum: float, maximum_inclusive: bool = True) -> float:
    if value is None:
        try:
            value = float(input(f"{label}: ").strip())
        except (EOFError, ValueError):
            raise ValueError(f"{label} must be a number") from None
    if not math.isfinite(value) or not (minimum <= value <= maximum if maximum_inclusive else minimum <= value < maximum):
        bound = f"[{minimum}, {maximum}{']' if maximum_inclusive else ')'}"
        raise ValueError(f"{label} must be in {bound}")
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description="Query nearby hazards by GPS; optionally filter hazards ahead by heading")
    parser.add_argument("--lat", type=float, help="Car latitude (-90 to 90)")
    parser.add_argument("--lon", type=float, help="Car longitude (-180 to 180)")
    parser.add_argument("--heading", type=float, help="Optional: direction 0=N, 90=E, 180=S, 270=W. Without it, search nearby.")
    parser.add_argument("--speed", type=float, default=0, help="Speed in meters/second (default: 0)")
    parser.add_argument("--include-advisories", action="store_true", help="Also include unconfirmed hazards")
    parser.add_argument("--url", default=os.getenv("ICVSP_API_URL", "http://127.0.0.1:8000"),
                        help="FastAPI base URL (default: http://127.0.0.1:8000)")
    args = parser.parse_args()

    try:
        lat = number_or_prompt(args.lat, "Latitude", minimum=-90, maximum=90)
        lon = number_or_prompt(args.lon, "Longitude", minimum=-180, maximum=180)
        heading = number_or_prompt(args.heading, "Heading degrees (0=N, 90=E)", minimum=0,
                                   maximum=360, maximum_inclusive=False)
        speed = number_or_prompt(args.speed, "Speed m/s", minimum=0, maximum=float("inf"))
    except ValueError as error:
        parser.error(str(error))

    # Location-only requests must not claim that a hazard is ahead of the vehicle.
    if heading is None:
        endpoint = args.url.rstrip("/") + "/hazards?" + urlencode({"lat": lat, "lon": lon})
        request = Request(endpoint, method="GET")
    else:
        payload = {
            "lat": lat,
            "lon": lon,
            "heading_deg": heading,
            "speed_mps": speed,
            "include_advisories": args.include_advisories,
        }
        endpoint = args.url.rstrip("/") + "/hazards/ahead"
        request = Request(endpoint, data=json.dumps(payload, allow_nan=False).encode("utf-8"),
                          headers={"Content-Type": "application/json"}, method="POST")
    print(f"Querying {endpoint} ...")
    try:
        with urlopen(request, timeout=15) as response:
            result = json.load(response)
    except HTTPError as error:
        try:
            details = error.read().decode("utf-8")
        except OSError:
            details = ""
        print(f"FastAPI returned HTTP {error.code}: {details}", file=sys.stderr)
        if error.code == 503:
            print("Check /ready and the server's DATABASE_URL / PostgreSQL credentials.", file=sys.stderr)
        if error.code == 401:
            print("For local-only testing, set ICVSP_ALLOW_ANONYMOUS=1 before starting FastAPI.",
                  file=sys.stderr)
        return 1
    except (URLError, TimeoutError, ValueError) as error:
        print(f"Request failed: {error}. Is FastAPI running?", file=sys.stderr)
        return 1

    print(json.dumps(result, ensure_ascii=False, indent=2))
    hazards = result if isinstance(result, list) else result.get("hazards", [])
    print(f"\nHazards ahead: {len(hazards)}")
    if not hazards:
        print("No matching hazards for this query (not necessarily an empty database).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
