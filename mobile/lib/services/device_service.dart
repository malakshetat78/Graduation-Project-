import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:http/http.dart' as http;
import 'package:supabase_flutter/supabase_flutter.dart';

import 'api_service.dart';

class DeviceService extends ChangeNotifier {
  DeviceService._();

  static final DeviceService instance = DeviceService._();

  // Only for local testing against the anonymous server.
  static const _testDeviceId = String.fromEnvironment('BEACON_DEVICE_ID');
  static const _requestTimeout = Duration(seconds: 10);

  final http.Client _client = http.Client();

  List<String> devices = [];
  bool loading = false;
  String? lastProblem;

  String get vehicleId => devices.isNotEmpty ? devices.first : _testDeviceId;

  Map<String, String> _headers() {
    final headers = {'Content-Type': 'application/json'};
    final token = Supabase.instance.client.auth.currentSession?.accessToken;

    if (token != null) {
      headers['Authorization'] = 'Bearer $token';
    }

    return headers;
  }

  Future<void> load() async {
    loading = true;
    lastProblem = null;
    notifyListeners();

    try {
      final response = await _client
          .get(Uri.parse('${ApiService.baseUrl}/me'), headers: _headers())
          .timeout(_requestTimeout);

      if (response.statusCode == 200) {
        final body = jsonDecode(utf8.decode(response.bodyBytes));
        final items = body is Map<String, dynamic> ? body['devices'] : null;

        devices = items is List ? items.whereType<String>().toList() : [];
      } else if (response.statusCode == 401) {
        lastProblem = 'Sign in again to see your devices.';
      } else {
        lastProblem = 'Could not load your devices.';
      }
    } on Exception {
      lastProblem = 'Could not load your devices.';
    } finally {
      loading = false;
      notifyListeners();
    }
  }

  /// Returns null on success, or a message to show.
  Future<String?> link(String deviceId, String pairingCode) async {
    try {
      final response = await _client
          .post(
            Uri.parse('${ApiService.baseUrl}/me/devices'),
            headers: _headers(),
            body: jsonEncode({
              'device_id': deviceId,
              'pairing_code': pairingCode,
            }),
          )
          .timeout(_requestTimeout);

      if (response.statusCode == 201) {
        await load();
        return null;
      }

      if (response.statusCode == 403) {
        return 'Wrong device ID or pairing code.';
      }

      if (response.statusCode == 401) {
        return 'Sign in again to link a device.';
      }

      return 'Could not link the device. Try again.';
    } on Exception {
      return 'Could not link the device. Try again.';
    }
  }
}
