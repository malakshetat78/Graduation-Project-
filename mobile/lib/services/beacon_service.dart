import 'dart:async';
import 'dart:convert';
import 'dart:math';

import 'package:flutter/foundation.dart';
import 'package:geolocator/geolocator.dart';
import 'package:http/http.dart' as http;
import 'package:supabase_flutter/supabase_flutter.dart';

import 'api_service.dart';

enum TripStatus { stopped, starting, active, noDevice, noLocation }

class BeaconService extends ChangeNotifier {
  BeaconService._();

  static final BeaconService instance = BeaconService._();

  static const _vehicleId = String.fromEnvironment('BEACON_DEVICE_ID');
  // Only for the local anonymous server, which refuses any token.
  static const _sendToken = bool.fromEnvironment(
    'BEACON_SEND_TOKEN',
    defaultValue: true,
  );
  static const _sendEvery = Duration(seconds: 10);
  static const _requestTimeout = Duration(seconds: 10);
  static const _maxBuffered = 3600;
  static const _finalStatuses = {400, 403, 413, 422};

  final http.Client _client = http.Client();
  final List<Map<String, Object>> _buffer = [];

  StreamSubscription<Position>? _positions;
  Timer? _timer;
  bool _sending = false;

  TripStatus status = TripStatus.stopped;
  int sentCount = 0;
  String? lastProblem;

  /// True only while the ROVO device is connected and detecting.
  bool detectorOn = false;

  bool get isActive => status == TripStatus.active;
  int get waitingCount => _buffer.length;

  void _setStatus(TripStatus value) {
    status = value;
    notifyListeners();
  }

  Future<void> start() async {
    if (isActive || status == TripStatus.starting) return;

    if (_vehicleId.isEmpty) {
      _setStatus(TripStatus.noDevice);
      return;
    }

    _setStatus(TripStatus.starting);

    if (!await _locationAllowed()) {
      _setStatus(TripStatus.noLocation);
      return;
    }

    sentCount = 0;
    lastProblem = null;

    _positions = Geolocator.getPositionStream(
      locationSettings: AndroidSettings(
        accuracy: LocationAccuracy.high,
        distanceFilter: 0,
        intervalDuration: const Duration(seconds: 1),
      ),
    ).listen(_addPosition, onError: (_) => _setStatus(TripStatus.noLocation));

    _timer = Timer.periodic(_sendEvery, (_) => unawaited(_flush()));
    _setStatus(TripStatus.active);
  }

  Future<void> stop() async {
    _timer?.cancel();
    _timer = null;
    await _positions?.cancel();
    _positions = null;
    await _flush();
    _setStatus(TripStatus.stopped);
  }

  Future<bool> _locationAllowed() async {
    if (!await Geolocator.isLocationServiceEnabled()) return false;

    var permission = await Geolocator.checkPermission();

    if (permission == LocationPermission.denied) {
      permission = await Geolocator.requestPermission();
    }

    return permission == LocationPermission.always ||
        permission == LocationPermission.whileInUse;
  }

  void _addPosition(Position position) {
    // No heading means no usable direction, so nothing is invented for it.
    if (position.heading < 0) return;

    _buffer.add({
      'vehicle_id': _vehicleId,
      't': position.timestamp.millisecondsSinceEpoch / 1000,
      'lat': position.latitude,
      'lon': position.longitude,
      'speed_mps': max(position.speed, 0.0),
      'heading_deg': position.heading % 360,
      'detector_on': detectorOn,
    });

    if (_buffer.length > _maxBuffered) {
      _buffer.removeAt(0);
    }

    notifyListeners();
  }

  Map<String, String> _headers() {
    final headers = {'Content-Type': 'application/json'};
    final token = Supabase.instance.client.auth.currentSession?.accessToken;

    if (_sendToken && token != null) {
      headers['Authorization'] = 'Bearer $token';
    }

    return headers;
  }

  Future<void> _flush() async {
    if (_sending || _buffer.isEmpty) return;

    _sending = true;
    final batch = List<Map<String, Object>>.of(_buffer);

    try {
      final response = await _client
          .post(
            Uri.parse('${ApiService.baseUrl}/beacons'),
            headers: _headers(),
            body: jsonEncode(batch),
          )
          .timeout(_requestTimeout);

      if (response.statusCode == 200) {
        _buffer.removeRange(0, min(batch.length, _buffer.length));
        sentCount += batch.length;
        lastProblem = null;
      } else if (response.statusCode == 401) {
        // Keep the positions: they can be sent after signing in again.
        lastProblem = 'Sign in again to share your position.';
      } else if (_finalStatuses.contains(response.statusCode)) {
        _buffer.removeRange(0, min(batch.length, _buffer.length));
        lastProblem = 'Server refused positions: ${response.statusCode}';
      } else {
        lastProblem = 'Server unavailable: ${response.statusCode}';
      }
    } on Exception {
      lastProblem = 'No connection. Positions are waiting.';
    } finally {
      _sending = false;
      notifyListeners();
    }
  }
}
