import 'dart:async';
import 'dart:convert';
import 'dart:math';

import 'package:http/http.dart' as http;

import 'api_service.dart';
import 'report_queue.dart';

class ReportUploader {
  ReportUploader._();
  static final ReportUploader instance = ReportUploader._();

  static const _requestTimeout = Duration(seconds: 10);
  static const _firstDelayMs = 2000;
  static const _maxDelayMs = 300000;
  static const _finalStatuses = {400, 401, 403, 409, 413, 422};

  final ReportQueue _queue = ReportQueue.instance;
  final http.Client _client = http.Client();
  final Random _random = Random();
  Timer? _timer;
  bool _running = false;
  bool _runAgain = false;
  int _failures = 0;

  void kick() {
    // A new BLE delivery can be processed even if older items are delayed.
    if (_timer?.isActive ?? false) return;
    unawaited(_run());
  }

  void retryNow() {
    _timer?.cancel();
    unawaited(_run());
  }

  Future<void> _run() async {
    if (_running) {
      _runAgain = true;
      return;
    }
    _running = true;
    try {
      await _queue.cleanUp();
      while (true) {
        final report = await _queue.nextQueued();
        if (report == null) break;
        final wait = await _send(report);
        if (wait != null) {
          // On retryable failure this report has a durable retry_at.
          // Continue any other due reports instead of blocking the queue.
          continue;
        }
      }
    } finally {
      _running = false;
    }
    if (_runAgain) {
      _runAgain = false;
      unawaited(_run());
      return;
    }
    final next = await _queue.nextWait();
    if (next != null) _schedule(next);
  }

  Duration _backoff() {
    final base = min(_firstDelayMs * (1 << min(_failures, 8)), _maxDelayMs);
    _failures++;
    return Duration(milliseconds: (base * (0.8 + _random.nextDouble() * 0.4)).round());
  }

  Future<Duration?> _send(QueuedReport report) async {
    http.Response response;
    try {
      response = await _client.post(
        Uri.parse('${ApiService.baseUrl}/reports'),
        headers: {'Content-Type': 'application/json'}, body: report.line,
      ).timeout(_requestTimeout);
    } on Exception {
      final delay = _backoff();
      await _queue.recordFailure(report.id, 'no connection', delay);
      return delay;
    }
    if (response.statusCode == 201) {
      final status = _bodyField(response, 'status');
      if (status == 'accepted' || status == 'duplicate') {
        _failures = 0;
        await _queue.markSent(report.id, status!);
        return null;
      }
      // Malformed ACK is not proof of a durable store.
      final delay = _backoff();
      await _queue.recordFailure(report.id, 'invalid 201 ACK', delay);
      return delay;
    }
    if (_finalStatuses.contains(response.statusCode) &&
        _bodyBool(response, 'retryable') == false) {
      _failures = 0;
      await _queue.markRejected(report.id,
        _bodyField(response, 'code') ?? 'HTTP ${response.statusCode}');
      return null;
    }
    var delay = _backoff();
    final retrySeconds = int.tryParse(response.headers['retry-after'] ?? '');
    if (retrySeconds != null && retrySeconds > 0) {
      final requested = Duration(seconds: retrySeconds);
      if (requested > delay) delay = requested;
    }
    await _queue.recordFailure(report.id, 'HTTP ${response.statusCode}', delay);
    return delay;
  }

  String? _bodyField(http.Response response, String field) {
    try {
      final body = jsonDecode(utf8.decode(response.bodyBytes));
      if (body is Map<String, dynamic> && body[field] is String) {
        return body[field] as String;
      }
    } on FormatException {
      return null;
    }
    return null;
  }

  bool? _bodyBool(http.Response response, String field) {
    try {
      final body = jsonDecode(utf8.decode(response.bodyBytes));
      if (body is Map<String, dynamic> && body[field] is bool) {
        return body[field] as bool;
      }
    } on FormatException {
      return null;
    }
    return null;
  }

  void _schedule(Duration delay) {
    _timer?.cancel();
    // No busy-loop when persisted deadline is nearly due.
    final effective = delay < const Duration(milliseconds: 100)
        ? const Duration(milliseconds: 100) : delay;
    _timer = Timer(effective, () {
      _timer = null;
      unawaited(_run());
    });
  }
}
