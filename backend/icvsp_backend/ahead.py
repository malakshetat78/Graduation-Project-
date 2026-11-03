"""Which nearby hazards are ahead of this user, on their road, in their direction of travel.

With a planned route the answer is exact for the route given: the hazard must lie on it, later than the user, and
the distance is measured along the road. Without one the backend does not know the next turn, so it looks along the
current heading only, within a corridor around it, and the distance is a straight line. A parallel street inside the
corridor cannot be told apart without map data; the corridor is a setting to tune on real roads."""
from __future__ import annotations

import math
from dataclasses import dataclass

from icvsp.geo import heading_diff, local_xy


@dataclass(frozen=True)
class AheadConfig:
    search_radius_m: float = 1000.0     # how far the database looks; not when the driver is warned
    alert_distance_m: float = 500.0     # the warning is raised inside this distance (starting value, to be tuned)
    alert_lead_s: float = 8.0           # ...or this long before arrival, if that is further (fast roads)
    alert_cooldown_s: float = 300.0     # the same hazard does not alert the same user again within this time
    corridor_m: float = 20.0            # lateral tolerance, GPS error and road width together, no route known
    corridor_growth: float = 0.02       # extra tolerance per metre ahead, for a gently curving road
    route_corridor_m: float = 25.0      # the hazard's distance from the route line
    off_route_m: float = 60.0           # further from the route than this: the route is out of date, ignore it


def bearing(x0: float, y0: float, x1: float, y1: float) -> float:
    return math.degrees(math.atan2(x1 - x0, y1 - y0)) % 360


def _project(px: float, py: float, a: tuple[float, float], b: tuple[float, float]) -> tuple[float, float]:
    """(fraction along segment ab, distance from the point to it)."""
    u = max(0.0, min(1.0, _fraction(px, py, a, b)))
    return u, math.hypot(px - (a[0] + u * (b[0] - a[0])), py - (a[1] + u * (b[1] - a[1])))


def _fraction(px: float, py: float, a: tuple[float, float], b: tuple[float, float]) -> float:
    """How far along the line through ab the point lies: below 0 before a, above 1 past b."""
    dx, dy = b[0] - a[0], b[1] - a[1]
    length2 = dx * dx + dy * dy
    return 0.0 if length2 == 0 else ((px - a[0]) * dx + (py - a[1]) * dy) / length2


class Route:
    """A planned route as a line in metres around the user's position, from the user's position onward."""

    def __init__(self, lat: float, lon: float, points: list[tuple[float, float]], cfg: AheadConfig):
        self.lat, self.lon, self.cfg = lat, lon, cfg
        xy = [local_xy(p[0], p[1], lat, lon) for p in points]
        best = min(range(len(xy) - 1), key=lambda i: _project(0.0, 0.0, xy[i], xy[i + 1])[1])
        u, self.offset_m = _project(0.0, 0.0, xy[best], xy[best + 1])
        start = (xy[best][0] + u * (xy[best + 1][0] - xy[best][0]), xy[best][1] + u * (xy[best + 1][1] - xy[best][1]))
        line = [start, *xy[best + 1:]]
        self.line = [p for i, p in enumerate(line) if i == 0 or p != line[i - 1]]

    @property
    def usable(self) -> bool:
        return self.offset_m <= self.cfg.off_route_m and math.dist(self.line[0], self.line[-1]) > 0

    def distance_to(self, h_lat: float, h_lon: float, h_heading: float, tol_deg: float) -> float | None:
        """Metres along the route to the first passage over the hazard in its direction; None if it is not on it.
        The corridor is measured sideways only: a hazard before the user or past the route's end is not on the route."""
        px, py = local_xy(h_lat, h_lon, self.lat, self.lon)
        walked = 0.0
        stretches = list(zip(self.line, self.line[1:]))
        for i, (a, b) in enumerate(stretches):
            seg = math.dist(a, b)
            along = _fraction(px, py, a, b)
            u, off = _project(px, py, a, b)
            if (off <= self.cfg.route_corridor_m and heading_diff(bearing(*a, *b), h_heading) <= tol_deg
                    and not (i == 0 and along < 0) and not (i == len(stretches) - 1 and along > 1)):
                return walked + u * seg
            walked += seg
        return None


def straight_ahead(lat: float, lon: float, heading: float, h_lat: float, h_lon: float, h_heading: float,
                   tol_deg: float, cfg: AheadConfig) -> float | None:
    """Metres straight ahead to the hazard, or None if it is behind, to the side or for the other direction."""
    if heading_diff(heading, h_heading) > tol_deg:
        return None
    x, y = local_xy(h_lat, h_lon, lat, lon)
    rad = math.radians(heading)
    along, across = x * math.sin(rad) + y * math.cos(rad), x * math.cos(rad) - y * math.sin(rad)
    if along <= 0 or abs(across) > cfg.corridor_m + cfg.corridor_growth * along:
        return None
    return math.hypot(along, across)
