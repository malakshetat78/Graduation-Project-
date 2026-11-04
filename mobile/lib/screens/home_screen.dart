import 'package:flutter/material.dart';

import '../core/app_colors.dart';
import '../services/beacon_service.dart';
import '../services/hazard_alert_service.dart';
import 'map_preview.dart';
import 'hazards_screen.dart';
import 'devices_screen.dart';
import 'reports_screen.dart';
import 'account_screen.dart';
import 'alert_screen.dart';
import 'trip_card.dart';

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  int _selectedIndex = 0;

  final HazardAlertService _alerts = HazardAlertService.instance;

  bool _showingAlert = false;

  @override
  void initState() {
    super.initState();
    _alerts.addListener(_showNextAlert);
  }

  @override
  void dispose() {
    _alerts.removeListener(_showNextAlert);
    _alerts.stop();
    BeaconService.instance.stop();
    super.dispose();
  }

  Future<void> _showNextAlert() async {
    if (_showingAlert || !mounted) return;

    final hazard = _alerts.takeAlert();

    if (hazard == null) return;

    _showingAlert = true;
    await Navigator.push(
      context,
      MaterialPageRoute(builder: (context) => AlertScreen(hazard: hazard)),
    );
    _showingAlert = false;
    await _showNextAlert();
  }

  void _showHazard(HazardAhead hazard) {
    showModalBottomSheet<void>(
      context: context,
      showDragHandle: true,
      backgroundColor: AppColors.cream,
      builder: (context) {
        return SafeArea(
          child: Padding(
            padding: const EdgeInsets.fromLTRB(24, 0, 24, 24),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  hazard.title,
                  style: const TextStyle(
                    color: AppColors.navy,
                    fontSize: 24,
                    fontWeight: FontWeight.bold,
                  ),
                ),
                const SizedBox(height: 8),
                Text(
                  hazard.evidenceLabel,
                  style: const TextStyle(color: AppColors.navy),
                ),
              ],
            ),
          ),
        );
      },
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
            child: ListenableBuilder(
              listenable: _alerts,
              builder: (context, _) {
                return MapPreview(
                  fix: _alerts.lastFix,
                  headingDeg: _alerts.headingDeg,
                  hazards: _alerts.hazards,
                  onHazardTap: _showHazard,
                );
              },
            ),
          ),
        ),

        const TripCard(),

        ListenableBuilder(listenable: _alerts, builder: _nextHazardCard),
      ],
    );
  }

  Widget _nextHazardCard(BuildContext context, Widget? child) {
    final warnings = _alerts.hazards.where((hazard) => !hazard.isAdvisory);
    final next = warnings.isEmpty ? null : warnings.first;

    final String title;
    final String detail;

    if (!_alerts.isActive) {
      title = 'No trip running';
      detail = 'Road alerts start with your trip.';
    } else if (next == null) {
      title = 'Nothing ahead';
      detail = _alerts.problem ?? 'No hazards reported on your road.';
    } else {
      title = next.title;
      detail = next.distanceBasis == 'route'
          ? '${next.aheadLabel} · ${next.evidenceLabel}'
          : next.evidenceLabel;
    }

    return Padding(
      padding: const EdgeInsets.all(20),
      child: Container(
        padding: const EdgeInsets.all(18),
        decoration: BoxDecoration(
          color: next?.inAlertRange ?? false
              ? const Color(0xFFFFE3DE)
              : Colors.white,
          borderRadius: BorderRadius.circular(20),
          border: Border.all(color: const Color(0xFFE1E5EA)),
        ),
        child: Row(
          children: [
            Icon(
              next == null
                  ? Icons.check_circle_outline
                  : Icons.warning_amber_rounded,
              color: next == null ? AppColors.navy : AppColors.coral,
              size: 42,
            ),
            const SizedBox(width: 14),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    title,
                    style: const TextStyle(
                      color: AppColors.navy,
                      fontSize: 18,
                      fontWeight: FontWeight.bold,
                    ),
                  ),
                  const SizedBox(height: 6),
                  Text(
                    detail,
                    style: const TextStyle(color: AppColors.navy, fontSize: 14),
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}
