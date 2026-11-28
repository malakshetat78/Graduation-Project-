-- Hazards around a position. SECURITY INVOKER: a phone calling this through Supabase still only gets what RLS
-- allows it (published, unexpired). This answers "what is nearby"; FastAPI decides what is actually ahead on
-- the user's road (icvsp_backend/ahead.py). The search radius here is not the alert distance.
create function public.nearby_hazards(
    p_lat double precision, p_lon double precision, p_radius_m double precision,
    p_states text[] default array['confirmed'], p_now timestamptz default now())
returns table (hazard_id uuid, type text, state text, severity text, latitude double precision,
               longitude double precision, heading_deg double precision, first_seen timestamptz,
               expires_at timestamptz, independent_sources integer, net_evidence double precision,
               sybil_suspected boolean, reasons jsonb, distance_m double precision)
language sql stable security invoker set search_path = public, extensions
as $$
    with origin as (select st_setsrid(st_makepoint(p_lon, p_lat), 4326)::geography as g)
    select h.hazard_id, h.type, h.state, h.severity, h.latitude, h.longitude, h.heading_deg, h.first_seen,
           h.expires_at, h.independent_sources, h.net_evidence, h.sybil_suspected, h.reasons,
           st_distance(h.location, origin.g) as distance_m
    from public.hazards h, origin
    where st_dwithin(h.location, origin.g, least(p_radius_m, 50000))
      and h.state = any (p_states)
      and h.expires_at > p_now
    order by distance_m;
$$;
revoke all on function public.nearby_hazards(double precision, double precision, double precision, text[], timestamptz)
    from public, anon;
grant execute on function public.nearby_hazards(double precision, double precision, double precision, text[], timestamptz)
    to authenticated, icvsp_backend;
