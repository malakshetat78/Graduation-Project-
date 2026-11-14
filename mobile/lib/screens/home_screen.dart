import 'package:flutter/material.dart';
import 'package:supabase_flutter/supabase_flutter.dart';

import '../core/app_colors.dart';
import 'live_map.dart';
import 'hazards_screen.dart';
import 'devices_screen.dart';
import 'reports_screen.dart';
import 'account_screen.dart';
import 'trip_card.dart';

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  int _selectedIndex = 0;

  String get _firstName {
    final name =
        Supabase.instance.client.auth.currentUser?.userMetadata?['full_name'];

    if (name is String && name.trim().isNotEmpty) {
      return name.trim().split(' ').first;
    }

    return 'Driver';
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
        Padding(
          padding: const EdgeInsets.fromLTRB(24, 24, 24, 18),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                'Hi, $_firstName',
                style: const TextStyle(
                  color: AppColors.navy,
                  fontSize: 28,
                  fontWeight: FontWeight.bold,
                ),
              ),
              const SizedBox(height: 6),
              const Text(
                'Your road. A little safer.',
                style: TextStyle(color: AppColors.navy, fontSize: 15),
              ),
            ],
          ),
        ),

        const Expanded(
          child: Padding(
            padding: EdgeInsets.symmetric(horizontal: 20),
            child: LiveMap(),
          ),
        ),

        const TripCard(),

        const SizedBox(height: 20),
      ],
    );
  }
}
