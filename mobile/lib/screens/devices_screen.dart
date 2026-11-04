import 'package:flutter/material.dart';

import '../core/app_colors.dart';

class DevicesScreen extends StatefulWidget {
  const DevicesScreen({super.key});

  @override
  State<DevicesScreen> createState() => _DevicesScreenState();
}

class _DevicesScreenState extends State<DevicesScreen> {
  bool _hasDemoDevice = true;

  void _showConnectionInfo() {
    showDialog<void>(
      context: context,
      builder: (context) {
        return AlertDialog(
          backgroundColor: AppColors.cream,
          title: const Text('Connect your ROVO unit'),
          content: const Text(
            'Device pairing will be added after the UI is complete. '
            'This preview does not connect to real hardware.',
          ),
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

  @override
  Widget build(BuildContext context) {
    return ListView(
      padding: const EdgeInsets.all(24),
      children: [
        const Text(
          'My devices',
          style: TextStyle(
            color: AppColors.navy,
            fontSize: 28,
            fontWeight: FontWeight.bold,
          ),
        ),

        const SizedBox(height: 8),

        const Text(
          'Your vehicle, connected.',
          style: TextStyle(color: AppColors.navy, fontSize: 15),
        ),

        const SizedBox(height: 8),

        const Text(
          'UI preview · No real device connection',
          style: TextStyle(color: Color(0xFF65758A), fontSize: 12),
        ),

        const SizedBox(height: 28),

        if (_hasDemoDevice)
          Material(
            color: Colors.white,
            shape: RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(24),
              side: const BorderSide(color: Color(0xFFE1E5EA)),
            ),
            clipBehavior: Clip.antiAlias,
            child: Padding(
              padding: const EdgeInsets.all(20),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Container(
                    height: 140,
                    width: double.infinity,
                    decoration: BoxDecoration(
                      color: AppColors.navy,
                      borderRadius: BorderRadius.circular(18),
                    ),
                    child: const Icon(
                      Icons.sensors,
                      color: AppColors.turquoise,
                      size: 76,
                    ),
                  ),

                  const SizedBox(height: 22),

                  const Text(
                    'ROVO Unit 01',
                    style: TextStyle(
                      color: AppColors.navy,
                      fontSize: 22,
                      fontWeight: FontWeight.bold,
                    ),
                  ),

                  const SizedBox(height: 12),

                  Container(
                    padding: const EdgeInsets.symmetric(
                      horizontal: 12,
                      vertical: 7,
                    ),
                    decoration: BoxDecoration(
                      color: const Color(0xFFEDF0F4),
                      borderRadius: BorderRadius.circular(20),
                    ),
                    child: const Text(
                      'Demo device',
                      style: TextStyle(color: AppColors.navy, fontSize: 12),
                    ),
                  ),

                  const SizedBox(height: 16),

                  const ListTile(
                    contentPadding: EdgeInsets.zero,
                    leading: Icon(
                      Icons.bluetooth_disabled,
                      color: AppColors.navy,
                    ),
                    title: Text('Not connected'),
                    subtitle: Text('Hardware pairing is not active yet'),
                  ),

                  const Divider(),

                  ListTile(
                    contentPadding: EdgeInsets.zero,
                    leading: const Icon(
                      Icons.settings_outlined,
                      color: AppColors.navy,
                    ),
                    title: const Text('Connection settings'),
                    trailing: const Icon(Icons.chevron_right),
                    onTap: _showConnectionInfo,
                  ),

                  TextButton.icon(
                    onPressed: () {
                      setState(() {
                        _hasDemoDevice = false;
                      });
                    },
                    icon: const Icon(Icons.link_off),
                    label: const Text('Remove demo device'),
                    style: TextButton.styleFrom(
                      foregroundColor: AppColors.navy,
                    ),
                  ),
                ],
              ),
            ),
          )
        else
          Container(
            padding: const EdgeInsets.all(28),
            decoration: BoxDecoration(
              color: Colors.white,
              borderRadius: BorderRadius.circular(24),
              border: Border.all(color: const Color(0xFFE1E5EA)),
            ),
            child: const Column(
              children: [
                Icon(Icons.sensors_off, size: 64, color: AppColors.navy),
                SizedBox(height: 18),
                Text(
                  'No devices yet',
                  style: TextStyle(
                    color: AppColors.navy,
                    fontSize: 22,
                    fontWeight: FontWeight.bold,
                  ),
                ),
                SizedBox(height: 10),
                Text(
                  'Add a demo device to preview its card.',
                  textAlign: TextAlign.center,
                  style: TextStyle(color: Color(0xFF65758A), height: 1.5),
                ),
              ],
            ),
          ),

        const SizedBox(height: 20),

        if (!_hasDemoDevice)
          SizedBox(
            height: 54,
            child: ElevatedButton.icon(
              onPressed: () {
                setState(() {
                  _hasDemoDevice = true;
                });
              },
              icon: const Icon(Icons.add),
              label: const Text('Add demo device'),
              style: ElevatedButton.styleFrom(
                backgroundColor: AppColors.turquoise,
                foregroundColor: AppColors.navy,
                elevation: 0,
                shape: RoundedRectangleBorder(
                  borderRadius: BorderRadius.circular(16),
                ),
              ),
            ),
          ),

        const SizedBox(height: 16),

        Material(
          color: Colors.transparent,
          child: ListTile(
            shape: RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(16),
            ),
            tileColor: Colors.white,
            leading: const Icon(Icons.help_outline, color: AppColors.navy),
            title: const Text('How to connect'),
            trailing: const Icon(Icons.chevron_right),
            onTap: _showConnectionInfo,
          ),
        ),
      ],
    );
  }
}
