import 'package:flutter/material.dart';

import '../core/app_colors.dart';
import '../services/beacon_service.dart';

class TripCard extends StatelessWidget {
  const TripCard({super.key});

  String _message(BeaconService service) {
    switch (service.status) {
      case TripStatus.active:
        return service.lastProblem ??
            'Sent ${service.sentCount} · Waiting ${service.waitingCount}';
      case TripStatus.starting:
        return 'Starting...';
      case TripStatus.noDevice:
        return 'Link a device before starting a trip.';
      case TripStatus.noLocation:
        return 'Location is unavailable.';
      case TripStatus.stopped:
        return 'Start a trip to share your position.';
    }
  }

  @override
  Widget build(BuildContext context) {
    final service = BeaconService.instance;

    return ListenableBuilder(
      listenable: service,
      builder: (context, _) {
        return Padding(
          padding: const EdgeInsets.fromLTRB(20, 16, 20, 0),
          child: Row(
            children: [
              Expanded(
                child: Text(
                  _message(service),
                  style: const TextStyle(color: AppColors.navy),
                ),
              ),
              const SizedBox(width: 12),
              FilledButton(
                onPressed: service.status == TripStatus.starting
                    ? null
                    : service.isActive
                    ? service.stop
                    : service.start,
                style: FilledButton.styleFrom(
                  backgroundColor: service.isActive
                      ? AppColors.coral
                      : AppColors.turquoise,
                  foregroundColor: AppColors.navy,
                ),
                child: Text(service.isActive ? 'End trip' : 'Start trip'),
              ),
            ],
          ),
        );
      },
    );
  }
}
