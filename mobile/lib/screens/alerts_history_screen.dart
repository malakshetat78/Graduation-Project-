import 'package:flutter/material.dart';

import '../core/app_colors.dart';

class AlertsHistoryScreen extends StatelessWidget {
  const AlertsHistoryScreen({super.key});

  static const List<Map<String, String>> _alerts = [
    {
      'title': 'Pothole ahead',
      'description': 'A pothole was reported 250 m ahead.',
      'time': 'Today · 10:30 AM',
      'status': 'Verified by 3 vehicles',
    },
    {
      'title': 'Road obstruction',
      'description': 'An obstruction was reported 1.2 km ahead.',
      'time': 'Today · 09:15 AM',
      'status': 'Under review',
    },
    {
      'title': 'Pothole ahead',
      'description': 'A pothole was reported 2.4 km ahead.',
      'time': 'Yesterday · 04:45 PM',
      'status': 'Verified by 4 vehicles',
    },
  ];

  void _showDetails(BuildContext context, Map<String, String> alert) {
    showDialog<void>(
      context: context,
      builder: (context) {
        return AlertDialog(
          backgroundColor: AppColors.cream,
          title: Text(alert['title']!),
          content: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(alert['description']!),
              const SizedBox(height: 16),
              Text(alert['status']!),
              const SizedBox(height: 12),
              Text(alert['time']!),
              const SizedBox(height: 16),
              const Text(
                'Sample history entry.',
                style: TextStyle(color: Color(0xFF65758A), fontSize: 12),
              ),
            ],
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context),
              child: const Text('Close'),
            ),
          ],
        );
      },
    );
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
                  'UI preview · Sample history',
                  style: TextStyle(color: Color(0xFF65758A), fontSize: 13),
                ),

                const SizedBox(height: 24),

                ..._alerts.map((alert) {
                  final underReview = alert['status'] == 'Under review';

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
                          alert['title']!,
                          style: const TextStyle(
                            color: AppColors.navy,
                            fontWeight: FontWeight.bold,
                          ),
                        ),
                        subtitle: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            const SizedBox(height: 8),

                            Text(alert['time']!),

                            const SizedBox(height: 12),

                            Container(
                              padding: const EdgeInsets.symmetric(
                                horizontal: 10,
                                vertical: 7,
                              ),
                              decoration: BoxDecoration(
                                color: underReview
                                    ? const Color(0xFFFFEAD0)
                                    : const Color(0xFFD8F8EF),
                                borderRadius: BorderRadius.circular(20),
                              ),
                              child: Text(
                                alert['status']!,
                                style: const TextStyle(
                                  color: AppColors.navy,
                                  fontSize: 12,
                                ),
                              ),
                            ),
                          ],
                        ),
                        trailing: const Icon(
                          Icons.chevron_right,
                          color: AppColors.navy,
                        ),
                        onTap: () => _showDetails(context, alert),
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
