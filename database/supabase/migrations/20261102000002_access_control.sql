-- Who may touch what. Default on Supabase is "anon and authenticated may reach every public table, subject to
-- RLS", so everything is revoked first and only what is listed here is opened again. Phones never write:
-- trust results, reputation and hazard states are changed by the backend role only.
do $$
begin
    if not exists (select from pg_roles where rolname = 'icvsp_backend') then
        -- no password here: the operator runs ALTER ROLE ... LOGIN PASSWORD once (database/README.md)
        create role icvsp_backend nologin noinherit;
    end if;
end;
$$;
grant usage on schema public, extensions to icvsp_backend;

do $$
declare t text;
begin
    foreach t in array array['profiles', 'devices', 'user_devices', 'device_counters', 'reports', 'beacons',
                             'evaluation_runs', 'hazards', 'hazard_evidence', 'hazard_passes',
                             'reputation_events', 'rejections', 'alert_deliveries']
    loop
        execute format('revoke all on public.%I from public, anon, authenticated, service_role', t);
        execute format('alter table public.%I enable row level security', t);
        execute format('create policy backend_all on public.%I for all to icvsp_backend using (true) with check (true)', t);
    end loop;
end;
$$;

-- the backend role: no TRUNCATE, no DDL, and DELETE only where retention or cleanup needs it
grant select, insert, update on public.profiles, public.devices, public.user_devices, public.device_counters,
    public.reports, public.hazards, public.reputation_events, public.alert_deliveries to icvsp_backend;
grant select, insert on public.evaluation_runs, public.rejections, public.beacons to icvsp_backend;
grant select, insert, delete on public.hazard_evidence, public.hazard_passes to icvsp_backend;
grant delete on public.reports, public.beacons, public.rejections, public.hazards, public.evaluation_runs
    to icvsp_backend;

revoke all on function public.handle_new_user() from public, anon, authenticated, service_role;

create function public.is_admin() returns boolean
    language sql stable security definer set search_path = ''
as $$
    select exists (select 1 from public.profiles where user_id = (select auth.uid()) and role = 'admin');
$$;
revoke all on function public.is_admin() from public, anon;
grant execute on function public.is_admin() to authenticated, icvsp_backend;

create function public.owns_device(p_device_id text) returns boolean
    language sql stable security definer set search_path = ''
as $$
    select exists (select 1 from public.user_devices
                   where user_id = (select auth.uid()) and device_id = p_device_id);
$$;
revoke all on function public.owns_device(text) from public, anon;
grant execute on function public.owns_device(text) to authenticated, icvsp_backend;

-- signed-in users read their own rows; there are no insert, update or delete policies for them anywhere
grant select on public.profiles, public.user_devices, public.reports, public.hazards to authenticated;
grant select (device_id, public_key, status, good, bad, registered_at) on public.devices to authenticated;
grant select on public.hazard_evidence, public.hazard_passes, public.rejections to authenticated;

create policy own_profile on public.profiles for select to authenticated
    using (user_id = (select auth.uid()) or (select public.is_admin()));
create policy own_links on public.user_devices for select to authenticated
    using (user_id = (select auth.uid()) or (select public.is_admin()));
create policy own_devices on public.devices for select to authenticated
    using (public.owns_device(device_id) or (select public.is_admin()));
create policy own_reports on public.reports for select to authenticated
    using (public.owns_device(device_id) or (select public.is_admin()));
-- a driver sees only hazards the engine published and that are still valid
create policy published_hazards on public.hazards for select to authenticated
    using ((state in ('corroborated', 'confirmed') and expires_at > now()) or (select public.is_admin()));
create policy admin_evidence on public.hazard_evidence for select to authenticated using ((select public.is_admin()));
create policy admin_passes on public.hazard_passes for select to authenticated using ((select public.is_admin()));
create policy admin_rejections on public.rejections for select to authenticated using ((select public.is_admin()));
