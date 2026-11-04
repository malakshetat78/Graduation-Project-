import 'dart:async';
import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';
import 'package:rovo_mobile/services/driver_location.dart';
import 'package:rovo_mobile/services/hazard_alert_service.dart';

/// A real answer from POST /hazards/ahead, captured from the backend.
const realAnswer = '''
{"hazards": [{"hazard_id": "70545bc7-da6e-5fdf-b2c6-31e7c128374b", "type": "pothole", "state": "corroborated", "level": "warning", "severity": "high", "latitude": 30.0444, "longitude": 31.2357, "heading_deg": 90.0, "distance_m": 450.0, "independent_sources": 2, "net_evidence": 1.024, "reasons": ["2 reports from 2 source(s) merged"], "expires_at": 1793851360.0040085, "distance_basis": "straight_line", "in_alert_range": true, "alert": true}], "search_radius_m": 1000.0, "alert_distance_m": 500.0, "route_status": "none"}
''';

String answer({bool alert = false, String level = 'warning'}) {
  final body = jsonDecode(realAnswer) as Map<String, dynamic>;
  final hazard = (body['hazards'] as List).first as Map<String, dynamic>;
  hazard['alert'] = alert;
  hazard['level'] = level;
  return jsonEncode(body);
}

const start = DriverFix(lat: 30.0, lon: 31.0, speedMps: 11, headingDeg: 90);

void main() {
  late StreamController<DriverFix> fixes;
  late List<http.Request> requests;
  late List<HazardAhead> recorded;
  late DateTime clock;
  late http.Response Function() reply;
  String? token;

  HazardAlertService service() {
    return HazardAlertService(
      client: MockClient((request) async {
        requests.add(request);
        return reply();
      }),
      token: () => token,
      fixes: () => fixes.stream,
      locationAllowed: () async => true,
      recordAlert: (hazard) async => recorded.add(hazard),
      now: () => clock,
    );
  }

  Future<void> send(DriverFix fix) async {
    fixes.add(fix);
    await pumpEventQueue();
  }

  setUp(() {
    fixes = StreamController();
    requests = [];
    recorded = [];
    clock = DateTime(2026, 11, 4, 12);
    token = 'user-token';
    reply = () => http.Response(answer(), 200);
  });

  // close() only completes once something has listened, so it is not awaited.
  tearDown(() => unawaited(fixes.close()));

  test('reads a real backend answer', () async {
    reply = () => http.Response(realAnswer, 200);
    final alerts = service();
    await alerts.start();
    await send(start);

    final hazard = alerts.hazards.single;
    expect(hazard.title, 'Pothole ahead');
    expect(hazard.distanceM, 450);
    expect(hazard.sources, 2);
    expect(hazard.inAlertRange, isTrue);
    expect(alerts.problem, isNull);
  });

  test('sends the token and what the contract asks for', () async {
    final alerts = service();
    await alerts.start();
    await send(start);

    final request = requests.single;
    expect(request.url.path, '/hazards/ahead');
    expect(request.headers['Authorization'], 'Bearer user-token');
    expect(jsonDecode(request.body), {
      'lat': 30.0,
      'lon': 31.0,
      'heading_deg': 90,
      'speed_mps': 11,
      'include_advisories': true,
    });
  });

  test('asks every 5 s, or sooner after moving 100 m', () async {
    final alerts = service();
    await alerts.start();
    await send(start);

    clock = clock.add(const Duration(seconds: 4));
    await send(start);
    expect(requests, hasLength(1));

    clock = clock.add(const Duration(seconds: 1));
    await send(start);
    expect(requests, hasLength(2));

    clock = clock.add(const Duration(seconds: 1));
    await send(
      const DriverFix(lat: 30.0, lon: 31.0015, speedMps: 11, headingDeg: 90),
    );
    expect(requests, hasLength(3));
  });

  test('warns only when alert is true, and records it', () async {
    reply = () => http.Response(answer(alert: true), 200);
    final alerts = service();
    await alerts.start();
    await send(start);

    expect(alerts.takeAlert()?.title, 'Pothole ahead');
    expect(alerts.takeAlert(), isNull);
    expect(recorded, hasLength(1));

    reply = () => http.Response(answer(), 200);
    clock = clock.add(const Duration(seconds: 5));
    await send(start);

    expect(alerts.takeAlert(), isNull);
    expect(recorded, hasLength(1));
    expect(alerts.hazards, hasLength(1));
  });

  test('an advisory is shown but never warns', () async {
    reply = () => http.Response(answer(level: 'advisory'), 200);
    final alerts = service();
    await alerts.start();
    await send(start);

    expect(alerts.hazards.single.isAdvisory, isTrue);
    expect(alerts.hazards.single.title, 'Possible pothole');
    expect(alerts.takeAlert(), isNull);
  });

  test('a warning survives a failure to save the history', () async {
    reply = () => http.Response(answer(alert: true), 200);
    final alerts = HazardAlertService(
      client: MockClient((request) async => reply()),
      token: () => token,
      fixes: () => fixes.stream,
      locationAllowed: () async => true,
      recordAlert: (hazard) async => throw Exception('disk full'),
      now: () => clock,
    );
    await alerts.start();
    await send(start);

    expect(alerts.takeAlert(), isNotNull);
  });

  test('without a sign-in it says so and sends nothing', () async {
    token = null;
    final alerts = service();
    await alerts.start();
    await send(start);

    expect(requests, isEmpty);
    expect(alerts.problem, 'Sign in to get road alerts.');
  });

  test('an expired sign-in is told apart from an outage', () async {
    reply = () => http.Response('{}', 401);
    final alerts = service();
    await alerts.start();
    await send(start);

    expect(alerts.problem, 'Sign in again to get road alerts.');
  });

  test('tells the driver alerts are down, then recovers', () async {
    final alerts = service();
    await alerts.start();
    await send(start);
    expect(alerts.hazards, hasLength(1));

    reply = () => throw http.ClientException('no network');
    clock = clock.add(const Duration(seconds: 5));
    await send(start);
    expect(alerts.problem, 'Road alerts unavailable. Trying again.');
    expect(alerts.hazards, hasLength(1));

    reply = () => http.Response(answer(), 200);
    clock = clock.add(const Duration(seconds: 5));
    await send(start);
    expect(alerts.problem, isNull);
  });

  test('a malformed answer is not trusted', () async {
    reply = () => http.Response('{"hazards":[{"hazard_id":1}]}', 200);
    final alerts = service();
    await alerts.start();
    await send(start);

    expect(alerts.problem, 'Road alerts unavailable. Trying again.');
    expect(alerts.hazards, isEmpty);
  });

  test('waits for a direction, then keeps the last one', () async {
    final alerts = service();
    await alerts.start();
    await send(const DriverFix(lat: 30.0, lon: 31.0, speedMps: 0));

    expect(requests, isEmpty);
    expect(alerts.problem, 'Waiting for your direction of travel.');

    await send(start);
    clock = clock.add(const Duration(seconds: 5));
    await send(const DriverFix(lat: 30.0, lon: 31.0, speedMps: 0));

    expect(requests, hasLength(2));
    expect(jsonDecode(requests.last.body)['heading_deg'], 90);
  });

  test('without location it does not start', () async {
    final alerts = HazardAlertService(
      client: MockClient((request) async => reply()),
      token: () => token,
      fixes: () => fixes.stream,
      locationAllowed: () async => false,
    );
    await alerts.start();

    expect(alerts.status, AlertStatus.noLocation);
  });

  test('ending the trip clears everything', () async {
    reply = () => http.Response(answer(alert: true), 200);
    final alerts = service();
    await alerts.start();
    await send(start);
    await alerts.stop();

    expect(alerts.status, AlertStatus.stopped);
    expect(alerts.hazards, isEmpty);
    expect(alerts.lastFix, isNull);
    expect(alerts.takeAlert(), isNull);
  });

  test('an answer arriving after the trip ended is ignored', () async {
    final answerLater = Completer<http.Response>();
    final alerts = HazardAlertService(
      client: MockClient((request) => answerLater.future),
      token: () => token,
      fixes: () => fixes.stream,
      locationAllowed: () async => true,
      recordAlert: (hazard) async => recorded.add(hazard),
      now: () => clock,
    );
    await alerts.start();
    await send(start);
    await alerts.stop();

    answerLater.complete(http.Response(answer(alert: true), 200));
    await pumpEventQueue();

    expect(alerts.hazards, isEmpty);
    expect(alerts.takeAlert(), isNull);
  });
}
