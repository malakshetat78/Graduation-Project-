-- ICVSP core schema: devices, signed reports, durable counter state, hazards and their evidence.
-- Field names follow the signed message (engine/schema/safety_event.schema.json): event_id is the guide's
-- message_id, boot_id + sequence are its counter, type is its event_type.
create extension if not exists postgis with schema extensions;

-- ---------- users ----------
create table public.profiles (
    user_id    uuid primary key references auth.users (id) on delete cascade,
    role       text not null default 'driver' check (role in ('driver', 'ambulance', 'admin')),
    created_at timestamptz not null default now()
);

-- every new Supabase Auth user gets a driver profile; only the backend can change the role
create function public.handle_new_user() returns trigger
    language plpgsql security definer set search_path = ''
as $$
begin
    insert into public.profiles (user_id) values (new.id) on conflict do nothing;
    return new;
end;
$$;
create trigger on_auth_user_created after insert on auth.users
    for each row execute function public.handle_new_user();

-- ---------- devices ----------
create table public.devices (
    device_id         text primary key check (char_length(device_id) between 1 and 64),
    public_key        text not null unique check (public_key ~ '^[0-9a-f]{64}$'),  -- Ed25519, hex
    status            text not null default 'ACTIVE' check (status in ('ACTIVE', 'REVOKED')),
    good              double precision not null default 0 check (good >= 0),       -- Beta reputation counts
    bad               double precision not null default 0 check (bad >= 0),
    pairing_code_hash text,                                                        -- one-time, cleared on use
    registered_at     timestamptz not null default now()
);

create table public.user_devices (
    user_id   uuid not null references auth.users (id) on delete cascade,
    device_id text not null references public.devices (device_id) on delete cascade,
    linked_at timestamptz not null default now(),
    primary key (user_id, device_id)
);
create index user_devices_device on public.user_devices (device_id);

-- Durable counter state, kept across backend restarts. A device's queue may deliver late and out of order,
-- so this is a record of the highest values seen, never a "must be higher" rule: replay protection is the
-- permanent uniqueness of (device_id, boot_id, sequence) on reports plus the freshness window.
create table public.device_counters (
    device_id        text primary key references public.devices (device_id) on delete cascade,
    highest_boot_id  bigint not null check (highest_boot_id >= 0),
    highest_sequence bigint not null check (highest_sequence >= 1),  -- within highest_boot_id
    reports_stored   bigint not null default 0 check (reports_stored >= 0),
    last_report_at   timestamptz not null
);

-- ---------- reports ----------
create table public.reports (
    device_id            text not null references public.devices (device_id) on delete restrict,
    event_id             text not null check (char_length(event_id) between 1 and 128),
    boot_id              bigint not null check (boot_id >= 0),
    sequence             bigint not null check (sequence >= 1),
    type                 text not null check (type in ('pothole', 'speed_bump')),
    severity             text not null check (severity in ('low', 'medium', 'high')),
    detection_confidence double precision not null check (detection_confidence between 0 and 1),
    gps_valid            boolean not null,
    latitude             double precision check (latitude between -90 and 90),
    longitude            double precision check (longitude between -180 and 180),
    position_accuracy_m  double precision check (position_accuracy_m > 0),
    speed_kmh            double precision check (speed_kmh >= 0),
    heading_deg          double precision check (heading_deg >= 0 and heading_deg < 360),
    imu_peak_g           double precision check (imu_peak_g >= 0),
    detected_at          timestamptz not null,
    sent_at              timestamptz not null,                     -- the signed utc_ms
    received_at          timestamptz not null default now(),
    line                 text not null check (octet_length(line) <= 2048),  -- signed report exactly as received
    location             extensions.geography(Point, 4326) generated always as (
        case when latitude is not null and longitude is not null
             then extensions.st_setsrid(extensions.st_makepoint(longitude, latitude), 4326)::extensions.geography
        end) stored,
    -- stored (ACK sent) is one fact, evaluated is a later one: pending means the trust engine has not run yet
    evaluation_status    text not null default 'pending'
        check (evaluation_status in ('pending', 'evaluated', 'rejected_by_engine', 'failed')),
    evaluation_reasons   jsonb not null default '[]',
    evaluated_at         timestamptz,
    settled              boolean not null default false,           -- reputation already updated from this report
    primary key (device_id, event_id),                             -- the guide's UNIQUE (device_id, message_id)
    unique (device_id, boot_id, sequence),
    check (gps_valid = (latitude is not null and longitude is not null and position_accuracy_m is not null))
);
create index reports_device_time on public.reports (device_id, detected_at desc);
create index reports_time on public.reports (detected_at);
create index reports_pending on public.reports (received_at) where evaluation_status = 'pending';
create index reports_location on public.reports using gist (location);

-- ---------- positions (needed for "drove past and saw nothing"; short retention, see Service.purge) ----------
create table public.beacons (
    vehicle_id  text not null references public.devices (device_id) on delete cascade,
    t           timestamptz not null,
    lat         double precision not null check (lat between -90 and 90),
    lon         double precision not null check (lon between -180 and 180),
    speed_mps   double precision not null check (speed_mps >= 0),
    heading_deg double precision not null check (heading_deg >= 0 and heading_deg < 360),
    detector_on boolean not null,
    primary key (vehicle_id, t)
);
create index beacons_time on public.beacons (t);

-- ---------- trust engine results ----------
create table public.evaluation_runs (          -- NFR-08: every decision traceable to its config version
    run_id      bigint generated always as identity primary key,
    ran_at      timestamptz not null,
    config_hash text not null,
    reports     integer not null,
    pending     integer not null
);

create table public.hazards (
    hazard_id           uuid primary key,
    type                text not null check (type in ('pothole', 'speed_bump')),
    state               text not null
        check (state in ('suppressed', 'unconfirmed', 'corroborated', 'confirmed', 'expired')),
    severity            text not null check (severity in ('unknown', 'low', 'medium', 'high')),
    latitude            double precision not null check (latitude between -90 and 90),
    longitude           double precision not null check (longitude between -180 and 180),
    heading_deg         double precision not null check (heading_deg >= 0 and heading_deg < 360),
    location            extensions.geography(Point, 4326) generated always as (
        extensions.st_setsrid(extensions.st_makepoint(longitude, latitude), 4326)::extensions.geography) stored,
    first_seen          timestamptz not null,
    expires_at          timestamptz not null,
    independent_sources integer not null check (independent_sources >= 0),
    net_evidence        double precision not null,
    sybil_suspected     boolean not null default false,
    reasons             jsonb not null default '[]',
    last_run_id         bigint not null references public.evaluation_runs (run_id),
    updated_at          timestamptz not null
);
create index hazards_location on public.hazards using gist (location);
create index hazards_state_expiry on public.hazards (state, expires_at);

-- the reports that support a hazard: one row per report, so a retry can never count as a second piece of evidence
create table public.hazard_evidence (
    hazard_id uuid not null references public.hazards (hazard_id) on delete cascade,
    device_id text not null,
    event_id  text not null,
    c         double precision not null,
    g         double precision not null,
    h         double precision not null,
    f         double precision not null,
    weight    double precision not null,
    primary key (hazard_id, device_id, event_id),
    foreign key (device_id, event_id) references public.reports (device_id, event_id) on delete cascade
);
create index hazard_evidence_report on public.hazard_evidence (device_id, event_id);

-- vehicles that drove over the spot and reported nothing (negative evidence, no report row behind it)
create table public.hazard_passes (
    hazard_id uuid not null references public.hazards (hazard_id) on delete cascade,
    device_id text not null references public.devices (device_id) on delete cascade,
    c         double precision not null,
    g         double precision not null,
    h         double precision not null,
    f         double precision not null,
    weight    double precision not null,
    primary key (hazard_id, device_id)
);

-- reputation ledger: the primary key is what makes "counted once" true across retries and restarts
create table public.reputation_events (
    device_id  text not null references public.devices (device_id) on delete cascade,
    hazard_id  uuid not null references public.hazards (hazard_id) on delete cascade,
    kind       text not null check (kind in ('report_confirmed', 'report_contradicted', 'missed_confirmed')),
    good_delta double precision not null,
    bad_delta  double precision not null,
    created_at timestamptz not null,
    primary key (device_id, hazard_id, kind)
);

-- ---------- security log and alert de-duplication ----------
create table public.rejections (
    rejection_id bigint generated always as identity primary key,
    received_at  timestamptz not null,
    device_id    text,
    event_id     text,
    code         text not null,
    reason       text not null
);
create index rejections_time on public.rejections (received_at);

create table public.alert_deliveries (          -- when each user last got an alert for each hazard, not where
    user_id         uuid not null references auth.users (id) on delete cascade,
    hazard_id       uuid not null references public.hazards (hazard_id) on delete cascade,
    last_alerted_at timestamptz not null,
    primary key (user_id, hazard_id)
);
