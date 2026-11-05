import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';
import 'package:path/path.dart' as p;
import 'package:rovo_mobile/services/report_queue.dart';
import 'package:rovo_mobile/services/report_uploader.dart';
import 'package:sqflite_common_ffi/sqflite_ffi.dart';

Uint8List reportLine(
  String eventId, {
  String type = 'pothole',
  DateTime? detectedAt,
}) {
  final time = (detectedAt ?? DateTime.now().toUtc()).toIso8601String();

  // The extra spaces prove the bytes are stored as they arrived.
  return Uint8List.fromList(
    utf8.encode(
      '{"event":  {"event_id":"$eventId","device_id":"car-1",'
      '"type":"$type","detected_at":"$time"},"signature":"abc"}',
    ),
  );
}

void main() {
  late ReportQueue queue;

  setUpAll(() {
    sqfliteFfiInit();
    databaseFactory = databaseFactoryFfi;
  });

  setUp(() {
    final folder = Directory.systemTemp.createTempSync('rovo_test');

    queue = ReportQueue(databasePath: p.join(folder.path, 'reports.db'));
  });

  test('stores the signed bytes unchanged', () async {
    final line = reportLine('e1');

    await queue.enqueue(line);

    expect((await queue.nextQueued())!.line, line);
  });

  test('ignores a report that is already stored', () async {
    expect(await queue.enqueue(reportLine('e1')), isTrue);
    expect(await queue.enqueue(reportLine('e1')), isFalse);
  });

  test('expires an old pothole', () async {
    await queue.enqueue(
      reportLine(
        'old',
        detectedAt: DateTime.now().toUtc().subtract(const Duration(hours: 48)),
      ),
    );

    await queue.cleanUp();

    expect(await queue.nextQueued(), isNull);
    expect((await queue.recent()).single.lastResult, 'expired');
  });

  test('drops the oldest pothole first when full', () async {
    await queue.enqueue(reportLine('bump', type: 'speed_bump'));

    for (var i = 0; i < 1000; i++) {
      await queue.enqueue(reportLine('pothole-$i'));
    }

    final first = jsonDecode(utf8.decode((await queue.nextQueued())!.line));

    expect((first as Map)['event']['event_id'], 'bump');
  }, timeout: const Timeout(Duration(minutes: 3)));

  test('marks a report sent after 201 and sends its exact bytes', () async {
    final line = reportLine('e1');
    late Uint8List sentBytes;

    final uploader = ReportUploader(
      queue: queue,
      client: MockClient((request) async {
        sentBytes = request.bodyBytes;
        return http.Response('{"status":"accepted"}', 201);
      }),
    );

    await queue.enqueue(line);
    await uploader.run();

    expect(sentBytes, line);
    expect(await queue.nextQueued(), isNull);
    expect((await queue.recent()).single.state, ReportQueue.stateSent);
  });

  test('moves a refused report out of the queue without retrying', () async {
    var requests = 0;

    final uploader = ReportUploader(
      queue: queue,
      client: MockClient((request) async {
        requests++;
        return http.Response('{"code":"bad_signature"}', 401);
      }),
    );

    await queue.enqueue(reportLine('e1'));
    await uploader.run();

    final report = (await queue.recent()).single;

    expect(requests, 1);
    expect(report.state, ReportQueue.stateRejected);
    expect(report.lastResult, 'bad_signature');
  });

  test(
    'keeps the report after a server error and honours Retry-After',
    () async {
      final uploader = ReportUploader(
        queue: queue,
        client: MockClient((request) async {
          return http.Response('', 503, headers: {'retry-after': '120'});
        }),
      );

      await queue.enqueue(reportLine('e1'));
      await uploader.run();
      uploader.dispose();

      final report = (await queue.recent()).single;

      expect(report.state, ReportQueue.stateQueued);
      expect(report.attempts, 1);
      expect(uploader.lastWait, const Duration(seconds: 120));
    },
  );

  test('keeps the report when the connection fails', () async {
    final uploader = ReportUploader(
      queue: queue,
      client: MockClient((request) async {
        throw http.ClientException('offline');
      }),
    );

    await queue.enqueue(reportLine('e1'));
    await uploader.run();
    uploader.dispose();

    expect((await queue.recent()).single.lastResult, 'no connection');
    expect(await queue.nextQueued(), isNotNull);
  });

  test('doubles the delay from 2 seconds up to 5 minutes with jitter', () {
    expect(ReportUploader.retryDelay(0, 0.5).inMilliseconds, 2000);
    expect(ReportUploader.retryDelay(1, 0.5).inMilliseconds, 4000);
    expect(ReportUploader.retryDelay(0, 0).inMilliseconds, 1600);
    expect(ReportUploader.retryDelay(0, 1).inMilliseconds, 2400);
    expect(ReportUploader.retryDelay(20, 0.5).inMilliseconds, 300000);
  });
}
