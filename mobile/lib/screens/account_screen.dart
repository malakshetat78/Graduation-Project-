import 'package:flutter/material.dart';
import 'package:supabase_flutter/supabase_flutter.dart';

import '../core/app_colors.dart';
import '../services/beacon_service.dart';
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
          title: const Text('Sign out?'),
          content: const Text('You will return to the welcome screen.'),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context, false),
              child: const Text('Cancel'),
            ),
            TextButton(
              onPressed: () => Navigator.pop(context, true),
              child: const Text('Sign out'),
            ),
          ],
        );
      },
    );

    if (!mounted || confirmed != true) return;

    if (BeaconService.instance.isActive) {
      await BeaconService.instance.stop();
    }

    await Supabase.instance.client.auth.signOut(scope: SignOutScope.local);

    if (!mounted) return;

    Navigator.pushAndRemoveUntil(
      context,
      MaterialPageRoute(builder: (context) => const WelcomeScreen()),
      (route) => false,
    );
  }

  @override
  Widget build(BuildContext context) {
    final email = Supabase.instance.client.auth.currentUser?.email ?? '';
    final initial = email.isEmpty ? 'R' : email[0].toUpperCase();

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
          child: Padding(
            padding: const EdgeInsets.all(20),
            child: Row(
              children: [
                CircleAvatar(
                  radius: 28,
                  backgroundColor: AppColors.navy,
                  child: Text(
                    initial,
                    style: const TextStyle(
                      color: AppColors.cream,
                      fontSize: 24,
                      fontWeight: FontWeight.bold,
                    ),
                  ),
                ),
                const SizedBox(width: 16),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      const Text(
                        'Signed in',
                        style: TextStyle(
                          color: AppColors.navy,
                          fontSize: 20,
                          fontWeight: FontWeight.bold,
                        ),
                      ),
                      const SizedBox(height: 5),
                      Text(
                        email,
                        style: const TextStyle(
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
                  _showInfo('Language', 'English is available now.');
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
                    'ROVO uses your location during a trip '
                        'to share vehicle positions.',
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
                    'ROVO helps drivers stay aware of road hazards.',
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
          label: const Text('Sign out'),
          style: OutlinedButton.styleFrom(
            foregroundColor: AppColors.navy,
            minimumSize: const Size(double.infinity, 54),
            side: const BorderSide(color: AppColors.navy),
            shape: RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(16),
            ),
          ),
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
