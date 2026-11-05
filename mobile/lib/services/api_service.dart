import 'dart:convert';

import 'package:http/http.dart' as http;

class ApiService {
  static const String baseUrl = String.fromEnvironment(
    'API_BASE_URL',
    defaultValue: 'http://10.0.2.2:8000',
  );

  Future<Map<String, dynamic>> _get(String path) async {
    final client = http.Client();

    try {
      final response = await client
          .get(
            Uri.parse('$baseUrl$path'),
            headers: {'Accept': 'application/json'},
          )
          .timeout(const Duration(seconds: 10));

      if (response.statusCode != 200) {
        throw Exception('Server returned ${response.statusCode}');
      }

      final data = jsonDecode(utf8.decode(response.bodyBytes));

      if (data is! Map<String, dynamic>) {
        throw const FormatException('Invalid server response');
      }

      return data;
    } finally {
      client.close();
    }
  }

  Future<bool> checkHealth() async {
    final data = await _get('/health');
    return data['status'] == 'ok';
  }

  Future<List<Map<String, String>>> getHazards() async {
    final data = await _get('/hazards');
    final items = data['hazards'];

    if (items is! List) {
      throw const FormatException('Invalid hazards list');
    }

    const fields = ['id', 'title', 'type', 'distance', 'status'];

    return items.map<Map<String, String>>((item) {
      if (item is! Map<String, dynamic>) {
        throw const FormatException('Invalid hazard');
      }

      final hazard = <String, String>{};

      for (final field in fields) {
        final value = item[field];

        if (value is! String || value.trim().isEmpty) {
          throw FormatException('Invalid hazard field: $field');
        }

        hazard[field] = value;
      }

      return hazard;
    }).toList();
  }
}
