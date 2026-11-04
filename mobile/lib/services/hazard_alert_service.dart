import 'dart:async';
import 'dart:collection';
import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:geolocator/geolocator.dart';
import 'package:http/http.dart' as http;
import 'package:supabase_flutter/supabase_flutter.dart';

import 'alert_history.dart';
import 'api_service.dart';
import 'driver_location.dart';

class HazardAhead {
  const HazardAhead({
    required this.id,
    required this.type,
    required this.level,
    required this.lat,
    required this.lon,
    required this.distanceM,
    required this.distanceBasis,
    required this.sources,
    required this.inAlertRange,
    required this.alert,
  });

  /// Reads one hazard from POST /hazards/ahead or GET /hazards. Only the first
  /// has distance_basis, in_alert_range and alert.
  factory HazardAhead.fromJson(Map<String, dynamic> json) {
    return HazardAhead(
      id: json['hazard_id'] as String,
      type: json['type'] as String,
      level: json['level'] as String,
      lat: (json['latitude'] as num).toDouble(),
      lon: (json['longitude'] as num).toDouble(),
      distanceM: (json['distance_m'] as num).toDouble(),
      distanceBasis: json['distance_basis'] as String? ?? 'straight_line',
      sources: json['independent_sources'] as int,
      inAlertRange: json['in_alert_range'] as bool? ?? false,
      alert: json['alert'] as bool? ?? false,
    );
  }

  final String id;
  final String type;
  final String level;
  final double lat;
  final double lon;
  final double distanceM;
  final String distanceBasis;
  final int sources;
  final bool inAlertRange;
  final bool alert;

  bool get isAdvisory => level == 'advisory';

  String get typeName => type == 'speed_bump' ? 'Speed bump' : 'Pothole';

  String get title {
    return isAdvisory
        ? 'Possible ${typeName.toLowerCase()}'
        : '$typeName ahead';
  }

  /// A straight-line distance is not the distance along the road, so the
  /// driver is told "Ahead" unless the backend measured along a route.
  String get aheadLabel {
    return distanceBasis == 'route' ? formatDistance(distanceM) : 'Ahead';
  }

  String get evidenceLabel {
    if (isAdvisory) return 'Not confirmed yet';
    return sources == 1
        ? 'Reported by 1 vehicle'
        : 'Reported by $sources vehicles';
  }
}

String formatDistance(double metres) {
  if (metres < 1000) return '${metres.round()} m';
  return '${(metres / 1000).toStringAsFixed(1)} km';
}

enum AlertStatus { stopped, starting, active, noLocation }

class HazardAlertService extends ChangeNotifier {
  HazardAlertService({
    http.Client? client,
    String? Function()? token,
    Stream<DriverFix> Function()? fixes,
    Future<bool> Function()? locationAllowed,
    Future<void> Function(HazardAhead)? recordAlert,
    DateTime Function()? now,
  }) : _client = client ?? http.Client(),
       _token = token ?? _sessionToken,
       _fixes = fixes ?? DriverLocation.fixes,
       _locationAllowed = locationAllowed ?? DriverLocation.allowed,
       _recordAlert = recordAlert ?? AlertHistory.instance.add,
       _now = now ?? DateTime.now;

  static final HazardAlertService instance = HazardAlertService();

  static const _askEvery = Duration(seconds: 5);
  static const _askAfterMetres = 100.0;
  static const _requestTimeout = Duration(seconds: 10);

  final http.Client _client;
  final String? Function() _token;
  final Stream<DriverFix> Function() _fixes;
  final Future<bool> Function() _locationAllowed;
  final Future<void> Function(HazardAhead) _recordAlert;
  final DateTime Function() _now;
  final Queue<HazardAhead> _alerts = Queue();

  StreamSubscription<DriverFix>? _subscription;
  DateTime? _askedAt;
  DriverFix? _askedFix;
  double? _lastHeading;
  bool _asking = false;
  int _trip = 0;

  AlertStatus status = AlertStatus.stopped;
  List<HazardAhead> hazards = const [];
  DriverFix? lastFix;
  String? problem;

  bool get isActive => status == AlertStatus.active;

  /// Direction used for the last question, also for drawing the road ahead.
  double? get headingDeg => _lastHeading;

  static String? _sessionToken() {
    return Supabase.instance.client.auth.currentSession?.accessToken;
  }

  void _setStatus(AlertStatus value) {
    status = value;
    notifyListeners();
  }

  /// The next alert to show the driver, oldest first.
  HazardAhead? takeAlert() {
    return _alerts.isEmpty ? null : _alerts.removeFirst();
  }

  Future<void> start() async {
    if (isActive || status == AlertStatus.starting) return;

    _setStatus(AlertStatus.starting);

    if (!await _locationAllowed()) {
      _setStatus(AlertStatus.noLocation);
      return;
    }

    _trip++;
    _askedAt = null;
    _askedFix = null;
    _lastHeading = null;
    problem = null;
    _subscription = _fixes().listen(
      _onFix,
      onError: (_) => _setStatus(AlertStatus.noLocation),
    );
    _setStatus(AlertStatus.active);
  }

  Future<void> stop() async {
    _trip++;
    await _subscription?.cancel();
    _subscription = null;
    hazards = const [];
    lastFix = null;
    problem = null;
    _alerts.clear();
    _setStatus(AlertStatus.stopped);
  }

  void _onFix(DriverFix fix) {
    lastFix = fix;
    _lastHeading = fix.headingDeg ?? _lastHeading;

    if (_asking) return;

    if (_lastHeading == null) {
      problem = 'Waiting for your direction of travel.';
      notifyListeners();
      return;
    }

    if (_isTimeToAsk(fix)) {
      unawaited(_ask(fix));
    } else {
      notifyListeners();
    }
  }

  bool _isTimeToAsk(DriverFix fix) {
    final askedAt = _askedAt;
    final askedFix = _askedFix;

    if (askedAt == null || askedFix == null) return true;
    if (_now().difference(askedAt) >= _askEvery) return true;

    return Geolocator.distanceBetween(
          askedFix.lat,
          askedFix.lon,
          fix.lat,
          fix.lon,
        ) >=
        _askAfterMetres;
  }

  Future<void> _ask(DriverFix fix) async {
    final token = _token();

    _askedAt = _now();
    _askedFix = fix;

    if (token == null) {
      problem = 'Sign in to get road alerts.';
      notifyListeners();
      return;
    }

    final trip = _trip;
    _asking = true;

    try {
      final response = await _client
          .post(
            Uri.parse('${ApiService.baseUrl}/hazards/ahead'),
            headers: {
              'Content-Type': 'application/json',
              'Authorization': 'Bearer $token',
            },
            body: jsonEncode({
              'lat': fix.lat,
              'lon': fix.lon,
              'heading_deg': _lastHeading,
              'speed_mps': fix.speedMps,
              'include_advisories': true,
            }),
          )
          .timeout(_requestTimeout);

      if (trip != _trip) return;

      if (response.statusCode == 200) {
        await _accept(response);
      } else if (response.statusCode == 401) {
        problem = 'Sign in again to get road alerts.';
      } else {
        problem = 'Road alerts unavailable. Trying again.';
      }
    } on Exception {
      if (trip == _trip) problem = 'Road alerts unavailable. Trying again.';
    } on TypeError {
      // A field of the wrong type: the answer cannot be trusted.
      if (trip == _trip) problem = 'Road alerts unavailable. Trying again.';
    } finally {
      _asking = false;
    }

    if (trip == _trip) notifyListeners();
  }

  Future<void> _accept(http.Response response) async {
    final body = jsonDecode(utf8.decode(response.bodyBytes));
    final items = (body as Map<String, dynamic>)['hazards'] as List<dynamic>;

    hazards = items
        .map((item) => HazardAhead.fromJson(item as Map<String, dynamic>))
        .toList();
    problem = null;

    for (final hazard in hazards.where((hazard) => hazard.alert)) {
      _alerts.add(hazard);

      try {
        await _recordAlert(hazard);
      } on Exception {
        // The driver still gets the warning if the history cannot be saved.
      }
    }
  }
}
