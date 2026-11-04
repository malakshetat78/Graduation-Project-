import 'package:flutter/material.dart';

import '../core/app_colors.dart';
import 'welcome_screen.dart';
import 'alerts_history_screen.dart';

class AccountScreen extends StatefulWidget {
  const AccountScreen({super.key});

  @override
  State<AccountScreen> createState() => _AccountScreenState();
}

class _AccountScreenState extends State<AccountScreen> {
  bool _alertSounds = true;
  bool _voiceAlerts = true;

  void _showInfo(String title, String message) {
    showDialog<void>(
      context: context,
      builder: (context) {
        return AlertDialog(
          backgroundColor: AppColors.cream,
          title: Text(title),
          content: Text(message),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context),
              child: const Text('Got it'),
            ),
          ],
        );
      },
    );
  }

  Future<void> _signOut() async {
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (context) {
        return AlertDialog(
          backgroundColor: AppColors.cream,
          title: const Text('Leave preview?'),
          content: const Text('You will return to the welcome screen.'),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context, false),
              child: const Text('Cancel'),
            ),
            TextButton(
              onPressed: () => Navigator.pop(context, true),
              child: const Text('Leave'),
            ),
          ],
        );
      },
    );

    if (!mounted || confirmed != true) return;

    Navigator.pushAndRemoveUntil(
      context,
      MaterialPageRoute(builder: (context) => const WelcomeScreen()),
      (route) => false,
    );
  }

  @override
  Widget build(BuildContext context) {
    return ListView(
      padding: const EdgeInsets.all(24),
      children: [
        const Text(
          'Account & settings',
          style: TextStyle(
            color: AppColors.navy,
            fontSize: 26,
            fontWeight: FontWeight.bold,
          ),
        ),

        const SizedBox(height: 26),

        _card(
          child: const Padding(
            padding: EdgeInsets.all(20),
            child: Row(
              children: [
                CircleAvatar(
                  radius: 28,
                  backgroundColor: AppColors.navy,
                  child: Text(
                    'U',
                    style: TextStyle(
                      color: AppColors.cream,
                      fontSize: 24,
                      fontWeight: FontWeight.bold,
                    ),
                  ),
                ),
                SizedBox(width: 16),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        'User',
                        style: TextStyle(
                          color: AppColors.navy,
                          fontSize: 20,
                          fontWeight: FontWeight.bold,
                        ),
                      ),
                      SizedBox(height: 5),
                      Text(
                        'user@example.com',
                        style: TextStyle(
                          color: Color(0xFF65758A),
                          fontSize: 13,
                        ),
                      ),
                    ],
                  ),
                ),
              ],
            ),
          ),
        ),

        const SizedBox(height: 24),

        _card(
          child: Column(
            children: [
              SwitchListTile(
                secondary: const Icon(
                  Icons.notifications_none_rounded,
                  color: AppColors.navy,
                ),
                title: const Text('Alert sounds'),
                value: _alertSounds,
                activeThumbColor: AppColors.navy,
                activeTrackColor: AppColors.turquoise,
                onChanged: (value) {
                  setState(() {
                    _alertSounds = value;
                  });
                },
              ),

              SwitchListTile(
                secondary: const Icon(
                  Icons.volume_up_outlined,
                  color: AppColors.navy,
                ),
                title: const Text('Voice alerts'),
                value: _voiceAlerts,
                activeThumbColor: AppColors.navy,
                activeTrackColor: AppColors.turquoise,
                onChanged: (value) {
                  setState(() {
                    _voiceAlerts = value;
                  });
                },
              ),

              const Divider(height: 1),

              ListTile(
                leading: const Icon(Icons.language, color: AppColors.navy),
                title: const Text('Language'),
                trailing: const Text('English'),
                onTap: () {
                  _showInfo(
                    'Language',
                    'English is available in this preview.',
                  );
                },
              ),

              ListTile(
                leading: const Icon(
                  Icons.location_on_outlined,
                  color: AppColors.navy,
                ),
                title: const Text('Location permissions'),
                trailing: const Icon(Icons.chevron_right),
                onTap: () {
                  _showInfo(
                    'Location permissions',
                    'This preview uses sample positions and '
                        'does not access your location.',
                  );
                },
              ),

              ListTile(
                leading: const Icon(
                  Icons.history_rounded,
                  color: AppColors.navy,
                ),
                title: const Text('Alert history'),
                trailing: const Icon(Icons.chevron_right),
                onTap: () {
                  Navigator.push(
                    context,
                    MaterialPageRoute(
                      builder: (context) => const AlertsHistoryScreen(),
                    ),
                  );
                },
              ),

              ListTile(
                leading: const Icon(Icons.help_outline, color: AppColors.navy),
                title: const Text('Help & support'),
                trailing: const Icon(Icons.chevron_right),
                onTap: () {
                  _showInfo(
                    'About ROVO',
                    'ROVO helps drivers stay aware of road hazards.\n\n'
                        'This version is a UI preview using sample data.',
                  );
                },
              ),
            ],
          ),
        ),

        const SizedBox(height: 24),

        OutlinedButton.icon(
          onPressed: _signOut,
          icon: const Icon(Icons.logout_rounded),
          label: const Text('Leave preview'),
          style: OutlinedButton.styleFrom(
            foregroundColor: AppColors.navy,
            minimumSize: const Size(double.infinity, 54),
            side: const BorderSide(color: AppColors.navy),
            shape: RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(16),
            ),
          ),
        ),

        const SizedBox(height: 18),

        const Text(
          'UI preview · Preferences reset when you leave',
          textAlign: TextAlign.center,
          style: TextStyle(color: Color(0xFF65758A), fontSize: 12),
        ),
      ],
    );
  }

  Widget _card({required Widget child}) {
    return Material(
      color: Colors.white,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(20),
        side: const BorderSide(color: Color(0xFFE1E5EA)),
      ),
      clipBehavior: Clip.antiAlias,
      child: child,
    );
  }
}
