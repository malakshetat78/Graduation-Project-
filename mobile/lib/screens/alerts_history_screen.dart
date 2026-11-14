import 'package:flutter/material.dart';

import '../core/app_colors.dart';
import '../services/alert_history.dart';

class AlertsHistoryScreen extends StatefulWidget {
  const AlertsHistoryScreen({super.key});

  @override
  State<AlertsHistoryScreen> createState() => _AlertsHistoryScreenState();
}

class _AlertsHistoryScreenState extends State<AlertsHistoryScreen> {
  final AlertHistory _history = AlertHistory.instance;

  List<AlertRecord>? _alerts;

  @override
  void initState() {
    super.initState();
    _history.addListener(_load);
    _load();
  }

  @override
  void dispose() {
    _history.removeListener(_load);
    super.dispose();
  }

  Future<void> _load() async {
    final alerts = await _history.recent();

    if (!mounted) return;

    setState(() {
      _alerts = alerts;
    });
  }

  String _time(DateTime at) {
    final local = at.toLocal();
    final minute = local.minute.toString().padLeft(2, '0');
    return '${local.day}/${local.month} · ${local.hour}:$minute';
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: AppColors.cream,
      appBar: AppBar(
        title: const Text('Alert history'),
        backgroundColor: AppColors.cream,
        foregroundColor: AppColors.navy,
        surfaceTintColor: Colors.transparent,
      ),
      body: SafeArea(
        child: Center(
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 480),
            child: ListView(
              padding: const EdgeInsets.all(24),
              children: [
                const Text(
                  'Your previous road alerts',
                  style: TextStyle(
                    color: AppColors.navy,
                    fontSize: 22,
                    fontWeight: FontWeight.bold,
                  ),
                ),

                const SizedBox(height: 10),

                const Text(
                  'Saved on this phone for 7 days.',
                  style: TextStyle(color: Color(0xFF65758A), fontSize: 13),
                ),

                const SizedBox(height: 24),

                if (_alerts?.isEmpty ?? false)
                  const Text(
                    'No road alerts yet.',
                    style: TextStyle(color: AppColors.navy),
                  ),

                ...?_alerts?.map((alert) {
                  return Padding(
                    padding: const EdgeInsets.only(bottom: 16),
                    child: Material(
                      color: Colors.white,
                      shape: RoundedRectangleBorder(
                        borderRadius: BorderRadius.circular(20),
                        side: const BorderSide(color: Color(0xFFE1E5EA)),
                      ),
                      clipBehavior: Clip.antiAlias,
                      child: ListTile(
                        contentPadding: const EdgeInsets.all(18),
                        leading: const Icon(
                          Icons.notifications_active_outlined,
                          color: AppColors.coral,
                          size: 30,
                        ),
                        title: Text(
                          alert.title,
                          style: const TextStyle(
                            color: AppColors.navy,
                            fontWeight: FontWeight.bold,
                          ),
                        ),
                        subtitle: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            const SizedBox(height: 8),
                            Text(_time(alert.at)),
                            const SizedBox(height: 8),
                            Text(alert.evidence),
                          ],
                        ),
                      ),
                    ),
                  );
                }),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
