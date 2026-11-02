# ICVSP database (Supabase)

`supabase/migrations` is the schema: run in order, nothing else is needed. Tested on `supabase/postgres:17.11.0.003` by
`backend/tests/test_postgres.py`.

| Table | What it holds | Who reads it directly |
|---|---|---|
| `profiles` | One row per Supabase user, created by a trigger; role driver / ambulance / admin | The user, admins |
| `devices`, `user_devices` | Registered devices and their keys, status, reputation; which user owns which device | Owner, admins (never `pairing_code_hash`) |
| `device_counters` | Highest boot and sequence seen per device, across restarts | Backend |
| `reports` | Signed reports exactly as received, the fields extracted from them, evaluation status | Owner, admins |
| `beacons` | Positions for negative evidence, 2 hours only | Backend |
| `hazards` | Current state of each hazard, `location` with a GiST index | Published and unexpired: any signed-in user. All: admins |
| `hazard_evidence`, `hazard_passes` | Which reports support a hazard, which vehicles drove past | Admins |
| `reputation_events` | Ledger that makes each reputation change happen once | Backend |
| `evaluation_runs` | Each trust-engine run and its configuration hash (NFR-08) | Backend |
| `rejections` | Refused reports with code and reason, 90 days | Admins |
| `alert_deliveries` | When a user was last alerted about a hazard (not where) | Backend |

Names follow the signed message, which the firmware fixes: the guide's `message_id` is `event_id`, its `counter` is
`boot_id` + `sequence`, its `timestamp` is `detected_at` (capture) and `sent_at` (signed `utc_ms`), its `event_type` is
`type`, its `confidence` is `detection_confidence`. The rule "UNIQUE (device_id, message_id)" is the primary key of
`reports`.

**Who can write.** Phones cannot write anything through Supabase: `anon` has no access at all and `authenticated` is
read-only, row by row. All changes go through FastAPI, connected as the role `icvsp_backend`. That role passes RLS, so
FastAPI checks each user's rights itself (`app.py`); it has no DDL rights, cannot truncate, and cannot delete devices or
profiles. `service_role` has been revoked on these tables as well, because nothing here uses that key.

## Setting up the hosted project

Requires the [Supabase CLI](https://supabase.com/docs/guides/cli).

```bash
cd database
supabase link --project-ref YOUR-PROJECT
supabase db push
```

Then, once, in the SQL editor, with a long random password that is stored only in the hosting platform's secrets:

```sql
alter role icvsp_backend login password '...';
```

`DATABASE_URL` uses that role (`backend/.env.example`). In the dashboard's Auth settings, turn on e-mail confirmation and
set the app's redirect URL, as in `supabase/config.toml`. Make the first administrator with `PUT /admin/users/{id}/role`
and the admin token. Never put the database password, the `service_role` key or the admin token in the app or the
repository; the app only needs the project URL and the public (anon / publishable) key.

## Local

```bash
cd database
supabase start     # full stack with Auth, needs Docker
```
