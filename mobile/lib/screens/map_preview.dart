import 'dart:math';

import 'package:flutter/material.dart';
import 'package:geolocator/geolocator.dart';

import '../core/app_colors.dart';
import '../services/driver_location.dart';
import '../services/hazard_alert_service.dart';

/// The road ahead as seen from the car: straight up is the direction of travel,
/// and the rings are 500 m and 1 km. It is not a street map.
class MapPreview extends StatelessWidget {
  const MapPreview({
    super.key,
    required this.fix,
    required this.headingDeg,
    required this.hazards,
    required this.onHazardTap,
  });

  static const _rangeM = 1000.0;
  static const _markerSize = 44.0;

  final DriverFix? fix;
  final double? headingDeg;
  final List<HazardAhead> hazards;
  final ValueChanged<HazardAhead> onHazardTap;

  /// Where a hazard sits on the view, from (0, 0) the car to (0, -1) straight
  /// ahead at the edge of the range. Null when the direction is not known yet.
  Offset? _place(HazardAhead hazard) {
    final fix = this.fix;
    final heading = headingDeg;

    if (fix == null || heading == null) return null;

    final metres = Geolocator.distanceBetween(
      fix.lat,
      fix.lon,
      hazard.lat,
      hazard.lon,
    );
    final bearing = Geolocator.bearingBetween(
      fix.lat,
      fix.lon,
      hazard.lat,
      hazard.lon,
    );
    final angle = (bearing - heading) * pi / 180;
    final reach = min(metres / _rangeM, 1.0);

    return Offset(sin(angle) * reach, -cos(angle) * reach);
  }

  Widget _marker(HazardAhead hazard, Offset place, Offset car, double radius) {
    final Color colour;

    if (hazard.isAdvisory) {
      colour = const Color(0xFFB8C2CF);
    } else {
      colour = hazard.inAlertRange ? AppColors.coral : const Color(0xFFFFB199);
    }

    return Positioned(
      left: car.dx + place.dx * radius - _markerSize / 2,
      top: car.dy + place.dy * radius - _markerSize / 2,
      width: _markerSize,
      height: _markerSize,
      child: IconButton.filled(
        tooltip: hazard.title,
        onPressed: () => onHazardTap(hazard),
        style: IconButton.styleFrom(
          backgroundColor: colour,
          foregroundColor: AppColors.navy,
        ),
        icon: Icon(
          hazard.isAdvisory ? Icons.help_outline : Icons.warning_amber_rounded,
          size: 24,
        ),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return ClipRRect(
      borderRadius: BorderRadius.circular(24),
      child: LayoutBuilder(
        builder: (context, box) {
          final car = Offset(box.maxWidth / 2, box.maxHeight * 0.85);
          final radius = min(box.maxWidth / 2, box.maxHeight * 0.85) - 24;

          return Stack(
            fit: StackFit.expand,
            children: [
              CustomPaint(painter: _RingsPainter(car, radius)),

              for (final hazard in hazards)
                if (_place(hazard) case final place?)
                  _marker(hazard, place, car, radius),

              Positioned(
                left: car.dx - 24,
                top: car.dy - 24,
                child: Container(
                  width: 48,
                  height: 48,
                  decoration: BoxDecoration(
                    color: const Color(0xFF008D91),
                    shape: BoxShape.circle,
                    border: Border.all(color: Colors.white, width: 3),
                  ),
                  child: const Icon(
                    Icons.navigation_rounded,
                    color: Colors.white,
                    size: 26,
                  ),
                ),
              ),

              Positioned(
                top: 14,
                left: 14,
                child: Container(
                  padding: const EdgeInsets.symmetric(
                    horizontal: 12,
                    vertical: 8,
                  ),
                  decoration: BoxDecoration(
                    color: Colors.white,
                    borderRadius: BorderRadius.circular(20),
                  ),
                  child: Text(
                    fix == null ? 'Start a trip' : 'Road ahead · 1 km',
                    style: const TextStyle(color: AppColors.navy, fontSize: 12),
                  ),
                ),
              ),
            ],
          );
        },
      ),
    );
  }
}

class _RingsPainter extends CustomPainter {
  const _RingsPainter(this.car, this.radius);

  final Offset car;
  final double radius;

  @override
  void paint(Canvas canvas, Size size) {
    canvas.drawRect(
      Offset.zero & size,
      Paint()..color = const Color(0xFFE8EDE4),
    );

    final ring = Paint()
      ..color = Colors.white
      ..style = PaintingStyle.stroke
      ..strokeWidth = 3;

    canvas.drawCircle(car, radius / 2, ring);
    canvas.drawCircle(car, radius, ring);
    canvas.drawLine(car, Offset(car.dx, car.dy - radius), ring);
  }

  @override
  bool shouldRepaint(covariant _RingsPainter oldDelegate) {
    return oldDelegate.car != car || oldDelegate.radius != radius;
  }
}
