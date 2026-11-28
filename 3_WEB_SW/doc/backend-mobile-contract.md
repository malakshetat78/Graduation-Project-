# Backend ↔ phone and device: delivery contract

For the Flutter app and the firmware's relay. The backend half is built and tested (`backend/tests/test_backend.py`,
`test_postgres.py`). The queues on the phone and the device are not built yet; this says what they must do.

## The message

The phone forwards the device's signed line **byte for byte**. The format is in
`engine/schema/safety_event.schema.json` and `firmware/espidf_security/docs/backend-integration.md`. It is not changed
for a retry: same ID, same content, same signature. Never re-serialise the JSON, or the signature fails.

## What each answer to `POST /reports` means

Decide by the HTTP status alone; `code` is for logs and screens.

| Answer | Meaning | What the phone does |
|---|---|---|
| `201`, `"status": "accepted"` | Durably stored for the first time | Delete from the queue |
| `201`, `"status": "duplicate"` | It was already stored (an earlier ACK was lost) | Delete from the queue |
| `400 413 401 403 409 422`, `"retryable": false` | Final: signature, device, format, ID conflict, too old or in the future | Move to a dead-letter list, stop retrying |
| `503` with `Retry-After`, `"retryable": true` | Nothing stored | Keep, retry later |
| `429`, any other `5xx`, timeout, no network | Unknown whether it was stored | Keep, retry later. A retry is always safe |

A 201 says the report is **stored**, nothing about the hazard: `"evaluation": "pending"` means the trust engine has not
run yet. Read the result later with `GET /reports/{device_id}/{event_id}` (`pending`, `evaluated`, `rejected_by_engine`).
An app must not show a report as a confirmed hazard because of its ACK.

## Who holds the message when

- BLE acknowledgement and backend ACK are different things. The device keeps a report until the phone's BLE
  acknowledgement says the phone has **written it to its own durable queue**. From then on the phone is responsible.
- The phone deletes only after a 201 (or a final refusal). Both queues must survive an app or device restart.

## Retry and queue settings (starting values, to tune by testing)

| Setting | Value | Why |
|---|---|---|
| Request timeout | 10 s | |
| Retry delay | 2 s doubling to 5 min, ±20 % random | Many phones reconnect at once after an outage |
| Honour `Retry-After` | Yes | |
| Phone queue size | 1000 reports | About 700 B each. When full, drop the oldest pothole report first |
| Time to live in a queue | Pothole 47 h, speed bump 13 days | One hour under the backend's limits below, so nothing is sent just to be refused |

## Time and counter policy

Delayed reports are normal, so the backend does **not** require counters to rise, and it does not compare a report with
the last one received.

| Check | Rule |
|---|---|
| Same ID, same content | Answered `duplicate`, at any age, so a phone can always clear its queue |
| Same ID, other content | Refused, `id_conflict` |
| Same device + boot + sequence under another ID | Refused, `counter_conflict` |
| `detected_at` and signed `utc_ms` too old | Refused, `stale`: pothole older than 48 h, speed bump older than 14 days |
| `detected_at` or signed `utc_ms` in the future | Refused beyond 30 s, `future` |
| Device restarted | A higher `boot_id`, accepted. Older boots still in a queue are accepted too |

Reports are kept for 15 days, longer than any window, so a replay is always found as already seen or refused as too old.
Different reports about the same hole are not duplicates: they are evidence, and the trust engine decides how they count.

## Hazards ahead: `POST /hazards/ahead`

Send about every 5 s while driving, or when the position has moved 100 m (with the user's token):

```json
{"lat": 30.0444, "lon": 31.2357, "heading_deg": 90, "speed_mps": 14,
 "route": [{"lat": 30.0444, "lon": 31.2357}, {"lat": 30.05, "lon": 31.24}]}
```

`route` only when the user chose a destination; from the current position onward. `heading_deg` is needed without a
route, and as the fallback when the user has left it (`"route_status": "off_route"`).

Show a warning when `alert` is `true`. The backend returns it once per hazard per user for 5 minutes, so the app does not
need to count repeats. `in_alert_range` stays true for drawing the hazard on a map. `distance_basis` is `route` (along the
road) or `straight_line` (no route: the next turn is unknown, so say "ahead", not an exact distance).

Search radius (1000 m) and alert distance (500 m, or 8 s at the current speed if that is further) are separate settings
in `AheadConfig`; the values are starting points to set by testing. Hazards behind the user, in the opposite direction,
on a parallel street more than about 20 m away, or expired are not returned. Without a route, a parallel street closer
than that cannot be told apart, because the backend has no map data.

## Sign-in

Sign-up, sign-in, sign-out, e-mail confirmation and password reset go straight from the app to Supabase Auth with the
public key. Send the access token as `Authorization: Bearer ...`. A `401` means sign in again; a `503` does not.
`POST /reports` needs no token, so an expired session never blocks the queue. To link a device, the owner enters the
one-time pairing code from provisioning: `POST /me/devices`.
