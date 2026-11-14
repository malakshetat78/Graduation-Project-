import 'dart:async';
import 'dart:convert';
import 'dart:math';

import 'package:http/http.dart' as http;

import 'api_service.dart';
import 'report_queue.dart';

class ReportUploader {
  ReportUploader({ReportQueue? queue, http.Client? client, Random? random})
    : _queue = queue ?? ReportQueue.instance,
      _client = client ?? http.Client(),
      _random = random ?? Random();

  static final ReportUploader instance = ReportUploader();

  static const _requestTimeout = Duration(seconds: 10);
  static const _firstDelayMs = 2000;
  static const _maxDelayMs = 300000;
  static const _finalStatuses = {400, 401, 403, 409, 413, 422};

  final ReportQueue _queue;
  final http.Client _client;
  final Random _random;

  Timer? _timer;
  bool _running = false;
  bool _runAgain = false;
  int _failures = 0;

  /// The wait before the next attempt, between 80% and 120% of the base.
  static Duration retryDelay(int failures, double randomUnit) {
    final doubledMs = _firstDelayMs * (1 << min(failures, 8));
    final baseMs = min(doubledMs, _maxDelayMs);

    return Duration(milliseconds: (baseMs * (0.8 + randomUnit * 0.4)).round());
  }

  /// Starts sending unless a retry delay is already being respected.
  void kick() {
    if (_timer?.isActive ?? false) return;
    unawaited(run());
  }

  void retryNow() {
    _timer?.cancel();
    unawaited(run());
  }

  void dispose() {
    _timer?.cancel();
  }

  Duration? lastWait;

  Future<void> run() async {
    if (_running) {
      _runAgain = true;
      return;
    }

    _running = true;
    Duration? wait;

    try {
      await _queue.cleanUp();

      while (wait == null) {
        final report = await _queue.nextQueued();

        if (report == null) break;

        wait = await _send(report);
      }
    } finally {
      _running = false;
    }

    if (wait != null) {
      _scheduleRetry(wait);
    } else if (_runAgain) {
      _runAgain = false;
      await run();
    }
  }

  /// Returns null when this report is finished, or the server's minimum
  /// wait when it stays in the queue.
  Future<Duration?> _send(QueuedReport report) async {
    final http.Response response;

    try {
      response = await _client
          .post(
            Uri.parse('${ApiService.baseUrl}/reports'),
            headers: {'Content-Type': 'application/json'},
            body: report.line,
          )
          .timeout(_requestTimeout);
    } on Exception {
      await _queue.recordFailure(report.id, 'no connection');
      return Duration.zero;
    }

    final status = response.statusCode;

    if (status == 201) {
      _failures = 0;
      await _queue.markSent(
        report.id,
        _bodyField(response, 'status') ?? 'accepted',
      );
      return null;
    }

    if (_finalStatuses.contains(status)) {
      _failures = 0;
      await _queue.markRejected(
        report.id,
        _bodyField(response, 'code') ?? 'HTTP $status',
      );
      return null;
    }

    await _queue.recordFailure(report.id, 'HTTP $status');

    final seconds = int.tryParse(response.headers['retry-after'] ?? '');
    return Duration(seconds: seconds ?? 0);
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

  void _scheduleRetry(Duration serverWait) {
    final delay = retryDelay(_failures, _random.nextDouble());
    final wait = serverWait > delay ? serverWait : delay;

    _failures++;
    lastWait = wait;
    _timer?.cancel();
    _timer = Timer(wait, () {
      unawaited(run());
    });
  }
}
