import 'package:flutter/material.dart';

import '../core/app_colors.dart';
import '../services/beacon_service.dart';
import '../services/hazard_alert_service.dart';

class TripCard extends StatelessWidget {
  const TripCard({super.key});

  /// Road alerts do not need a linked device, so a trip can start without one.
  String _message(HazardAlertService alerts, BeaconService beacons) {
    if (alerts.status == AlertStatus.noLocation) {
      return 'Location is unavailable.';
    }
    if (alerts.status == AlertStatus.starting) return 'Starting...';
    if (!alerts.isActive) return 'Start a trip to get road alerts.';
    if (alerts.problem != null) return alerts.problem!;

    switch (beacons.status) {
      case TripStatus.active:
        return beacons.lastProblem ??
            'Road alerts on · Sent ${beacons.sentCount} · '
                'Waiting ${beacons.waitingCount}';
      case TripStatus.noDevice:
        return 'Road alerts on. Link a device to share your position.';
      case TripStatus.noLocation:
      case TripStatus.starting:
      case TripStatus.stopped:
        return 'Road alerts on.';
    }
  }

  void _toggle(HazardAlertService alerts, BeaconService beacons) {
    if (alerts.isActive) {
      alerts.stop();
      beacons.stop();
    } else {
      alerts.start();
      beacons.start();
    }
  }

  @override
  Widget build(BuildContext context) {
    final alerts = HazardAlertService.instance;
    final beacons = BeaconService.instance;

    return ListenableBuilder(
      listenable: Listenable.merge([alerts, beacons]),
      builder: (context, _) {
        return Padding(
          padding: const EdgeInsets.fromLTRB(20, 16, 20, 0),
          child: Row(
            children: [
              Expanded(
                child: Text(
                  _message(alerts, beacons),
                  style: const TextStyle(color: AppColors.navy),
                ),
              ),
              const SizedBox(width: 12),
              FilledButton(
                onPressed: alerts.status == AlertStatus.starting
                    ? null
                    : () => _toggle(alerts, beacons),
                style: FilledButton.styleFrom(
                  backgroundColor: alerts.isActive
                      ? AppColors.coral
                      : AppColors.turquoise,
                  foregroundColor: AppColors.navy,
                ),
                child: Text(alerts.isActive ? 'End trip' : 'Start trip'),
              ),
            ],
          ),
        );
      },
    );
  }
}
