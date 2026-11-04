import 'dart:convert';

import 'package:http/http.dart' as http;

import 'driver_location.dart';
import 'hazard_alert_service.dart';

class ApiService {
  static const String baseUrl = String.fromEnvironment(
    'API_BASE_URL',
    defaultValue: 'http://10.0.2.2:8000',
  );

  Future<dynamic> _get(String path, {String? token}) async {
    final client = http.Client();

    try {
      final response = await client
          .get(
            Uri.parse('$baseUrl$path'),
            headers: {
              'Accept': 'application/json',
              if (token != null) 'Authorization': 'Bearer $token',
            },
          )
          .timeout(const Duration(seconds: 10));

      if (response.statusCode != 200) {
        throw Exception('Server returned ${response.statusCode}');
      }

      return jsonDecode(utf8.decode(response.bodyBytes));
    } finally {
      client.close();
    }
  }

  Future<bool> checkHealth() async {
    final data = await _get('/health');
    return data is Map<String, dynamic> && data['status'] == 'ok';
  }

  /// Hazards around a position, for browsing. It never raises an alert or
  /// starts the alert cooldown, unlike POST /hazards/ahead.
  Future<List<HazardAhead>> getNearbyHazards(
    DriverFix fix,
    String token,
  ) async {
    final data = await _get(
      '/hazards?lat=${fix.lat}&lon=${fix.lon}&radius_m=1000',
      token: token,
    );

    if (data is! List) {
      throw const FormatException('Invalid hazards list');
    }

    return data.map((item) {
      return HazardAhead.fromJson(item as Map<String, dynamic>);
    }).toList();
  }
}
