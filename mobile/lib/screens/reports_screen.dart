import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;

import '../core/app_colors.dart';
import '../services/report_queue.dart';
import '../services/report_uploader.dart';

class ReportsScreen extends StatefulWidget {
  const ReportsScreen({super.key});

  @override
  State<ReportsScreen> createState() => _ReportsScreenState();
}

class _ReportsScreenState extends State<ReportsScreen> {
  static const _testReportsUrl = 'http://10.0.2.2:8010/test_reports.jsonl';

  final ReportQueue _queue = ReportQueue.instance;

  List<ReportSummary> _reports = [];
  bool _adding = false;

  @override
  void initState() {
    super.initState();
    _queue.addListener(_load);
    _load();
  }

  @override
  void dispose() {
    _queue.removeListener(_load);
    super.dispose();
  }

  Future<void> _load() async {
    final reports = await _queue.recent();

    if (!mounted) return;

    setState(() {
      _reports = reports;
    });
  }

  List<Uint8List> _splitLines(Uint8List bytes) {
    final lines = <Uint8List>[];
    var start = 0;

    for (var i = 0; i <= bytes.length; i++) {
      if (i == bytes.length || bytes[i] == 10) {
        if (i > start) {
          lines.add(Uint8List.fromList(bytes.sublist(start, i)));
        }
        start = i + 1;
      }
    }

    return lines;
  }

  Future<void> _addTestReports() async {
    setState(() {
      _adding = true;
    });

    var message = '';

    try {
      final response = await http
          .get(Uri.parse(_testReportsUrl))
          .timeout(const Duration(seconds: 10));

      if (response.statusCode != 200) {
        throw Exception('HTTP ${response.statusCode}');
      }

      var added = 0;

      for (final line in _splitLines(response.bodyBytes)) {
        if (await _queue.enqueue(line)) added++;
      }

      ReportUploader.instance.kick();
      message = 'Added $added test reports.';
    } on Exception {
      message = 'Could not load test reports.';
    }

    if (!mounted) return;

    setState(() {
      _adding = false;
    });

    ScaffoldMessenger.of(context)
        .showSnackBar(SnackBar(content: Text(message)));
  }

  int _count(String state) {
    return _reports.where((report) => report.state == state).length;
  }

  Widget _countTile(String label, int value) {
    return Expanded(
      child: Column(
        children: [
          Text(
            '$value',
            style: const TextStyle(
              color: AppColors.navy,
              fontSize: 24,
              fontWeight: FontWeight.bold,
            ),
          ),
          const SizedBox(height: 4),
          Text(label, style: const TextStyle(color: AppColors.navy)),
        ],
      ),
    );
  }

  String _stateLabel(ReportSummary report) {
    switch (report.state) {
      case ReportQueue.stateSent:
        return report.lastResult == 'duplicate'
            ? 'Sent · Already stored'
            : 'Sent · Waiting for evaluation';
      case ReportQueue.stateRejected:
        return 'Rejected · ${report.lastResult ?? 'unknown'}';
      default:
        return report.attempts == 0
            ? 'Waiting to send'
            : 'Retrying · ${report.lastResult ?? 'unknown'}';
    }
  }

  IconData _stateIcon(String state) {
    switch (state) {
      case ReportQueue.stateSent:
        return Icons.cloud_done_outlined;
      case ReportQueue.stateRejected:
        return Icons.block;
      default:
        return Icons.cloud_upload_outlined;
    }
  }

  Widget _reportCard(ReportSummary report) {
    return Card(
      color: Colors.white,
      elevation: 0,
      margin: const EdgeInsets.only(bottom: 12),
      child: ListTile(
        leading: Icon(_stateIcon(report.state), color: AppColors.navy),
        title: Text(
          report.type == 'speed_bump' ? 'Speed bump' : 'Pothole',
          style: const TextStyle(
            color: AppColors.navy,
            fontWeight: FontWeight.bold,
          ),
        ),
        subtitle: Text(_stateLabel(report)),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final waiting = _count(ReportQueue.stateQueued);

    return ListView(
      padding: const EdgeInsets.all(24),
      children: [
        const Text(
          'Trip reports',
          style: TextStyle(
            color: AppColors.navy,
            fontSize: 28,
            fontWeight: FontWeight.bold,
          ),
        ),
        const SizedBox(height: 20),
        Row(
          children: [
            _countTile('Waiting', waiting),
            _countTile('Sent', _count(ReportQueue.stateSent)),
            _countTile('Rejected', _count(ReportQueue.stateRejected)),
          ],
        ),
        const SizedBox(height: 20),
        if (waiting > 0)
          OutlinedButton(
            onPressed: ReportUploader.instance.retryNow,
            child: const Text('Retry now'),
          ),
        if (kDebugMode)
          TextButton(
            onPressed: _adding ? null : _addTestReports,
            child: const Text('Add test reports'),
          ),
        const SizedBox(height: 12),
        if (_reports.isEmpty)
          const Padding(
            padding: EdgeInsets.all(32),
            child: Text(
              'No reports yet.',
              textAlign: TextAlign.center,
              style: TextStyle(color: AppColors.navy),
            ),
          )
        else
          ..._reports.map(_reportCard),
      ],
    );
  }
}
