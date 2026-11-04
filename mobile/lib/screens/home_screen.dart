import 'package:flutter/material.dart';

import '../core/app_colors.dart';
import 'map_preview.dart';
import 'hazards_screen.dart';
import 'devices_screen.dart';
import 'reports_screen.dart';
import 'account_screen.dart';
import 'alert_screen.dart';

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  int _selectedIndex = 0;

  void _openAlert() {
    Navigator.push(
      context,
      MaterialPageRoute(builder: (context) => const AlertScreen()),
    );
  }

  @override
  Widget build(BuildContext context) {
    final pages = [
      _mapPage(),
      const HazardsScreen(),
      const DevicesScreen(),
      const ReportsScreen(),
      const AccountScreen(),
    ];

    return Center(
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 480),
        child: Scaffold(
          backgroundColor: AppColors.cream,
          body: SafeArea(
            child: IndexedStack(index: _selectedIndex, children: pages),
          ),
          bottomNavigationBar: NavigationBar(
            selectedIndex: _selectedIndex,
            onDestinationSelected: (index) {
              setState(() {
                _selectedIndex = index;
              });
            },
            backgroundColor: Colors.white,
            indicatorColor: AppColors.turquoise,
            destinations: const [
              NavigationDestination(
                icon: Icon(Icons.map_outlined),
                selectedIcon: Icon(Icons.map),
                label: 'Map',
              ),
              NavigationDestination(
                icon: Icon(Icons.warning_amber_rounded),
                label: 'Hazards',
              ),
              NavigationDestination(
                icon: Icon(Icons.sensors),
                label: 'Devices',
              ),
              NavigationDestination(
                icon: Icon(Icons.receipt_long_outlined),
                selectedIcon: Icon(Icons.receipt_long),
                label: 'Reports',
              ),
              NavigationDestination(
                icon: Icon(Icons.person_outline_rounded),
                selectedIcon: Icon(Icons.person),
                label: 'Account',
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _mapPage() {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const Padding(
          padding: EdgeInsets.fromLTRB(24, 24, 24, 18),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                'Hi, User',
                style: TextStyle(
                  color: AppColors.navy,
                  fontSize: 28,
                  fontWeight: FontWeight.bold,
                ),
              ),
              SizedBox(height: 6),
              Text(
                'Your road. A little safer.',
                style: TextStyle(color: AppColors.navy, fontSize: 15),
              ),
            ],
          ),
        ),

        Expanded(
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 20),
            child: Stack(
              fit: StackFit.expand,
              children: [
                MapPreview(onHazardTap: _openAlert),
                Positioned(
                  right: 16,
                  bottom: 16,
                  child: FloatingActionButton.small(
                    tooltip: 'Preview road alert',
                    backgroundColor: AppColors.turquoise,
                    foregroundColor: AppColors.navy,
                    onPressed: _openAlert,
                    child: const Icon(Icons.notifications_active_outlined),
                  ),
                ),
              ],
            ),
          ),
        ),

        Padding(
          padding: const EdgeInsets.all(20),
          child: Container(
            padding: const EdgeInsets.all(18),
            decoration: BoxDecoration(
              color: Colors.white,
              borderRadius: BorderRadius.circular(20),
              border: Border.all(color: const Color(0xFFE1E5EA)),
            ),
            child: const Row(
              children: [
                Icon(
                  Icons.warning_amber_rounded,
                  color: AppColors.coral,
                  size: 42,
                ),
                SizedBox(width: 14),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        'Pothole ahead',
                        style: TextStyle(
                          color: AppColors.navy,
                          fontSize: 18,
                          fontWeight: FontWeight.bold,
                        ),
                      ),
                      SizedBox(height: 6),
                      Text(
                        '250 m · Your direction',
                        style: TextStyle(color: AppColors.navy, fontSize: 14),
                      ),
                      SizedBox(height: 6),
                      Text(
                        'Sample alert',
                        style: TextStyle(
                          color: Color(0xFF65758A),
                          fontSize: 12,
                        ),
                      ),
                    ],
                  ),
                ),
              ],
            ),
          ),
        ),
      ],
    );
  }
}
