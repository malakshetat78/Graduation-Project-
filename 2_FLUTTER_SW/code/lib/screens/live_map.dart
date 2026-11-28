import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_map/flutter_map.dart';
import 'package:geolocator/geolocator.dart';
import 'package:latlong2/latlong.dart';

import '../core/app_colors.dart';
import '../services/hazard_alert_service.dart';

/// The driver's position on a street map, with the hazards ahead as tappable
/// markers. While following, the camera keeps the car and the nearest hazard
/// ahead on screen together.
class LiveMap extends StatefulWidget {
  const LiveMap({super.key, this.hazards = const [], this.onHazardTap});

  final List<HazardAhead> hazards;
  final ValueChanged<HazardAhead>? onHazardTap;

  @override
  State<LiveMap> createState() => _LiveMapState();
}

class _LiveMapState extends State<LiveMap> {
  static final LatLng _cairo = LatLng(30.0444, 31.2357);
  static const double _zoom = 16;
  static const double _maxFitZoom = 17;
  static const EdgeInsets _fitPadding = EdgeInsets.fromLTRB(56, 72, 56, 72);

  final MapController _mapController = MapController();

  StreamSubscription<Position>? _positions;
  LatLng? _current;
  bool _mapReady = false;
  bool _following = true;
  String? _problem;

  @override
  void initState() {
    super.initState();
    _startLocation();
  }

  @override
  void didUpdateWidget(LiveMap oldWidget) {
    super.didUpdateWidget(oldWidget);

    if (_nearest(oldWidget.hazards)?.id != _nearest(widget.hazards)?.id) {
      _follow();
    }
  }

  @override
  void dispose() {
    _positions?.cancel();
    _mapController.dispose();
    super.dispose();
  }

  Future<void> _startLocation() async {
    if (!await Geolocator.isLocationServiceEnabled()) {
      _showProblem('Location is turned off.');
      return;
    }

    var permission = await Geolocator.checkPermission();

    if (permission == LocationPermission.denied) {
      permission = await Geolocator.requestPermission();
    }

    if (permission != LocationPermission.always &&
        permission != LocationPermission.whileInUse) {
      _showProblem('Location permission is needed to show your position.');
      return;
    }

    if (!mounted) return;

    // Same settings as BeaconService, because Geolocator shares one stream.
    _positions =
        Geolocator.getPositionStream(
          locationSettings: AndroidSettings(
            accuracy: LocationAccuracy.high,
            distanceFilter: 0,
            intervalDuration: const Duration(seconds: 1),
            // Keeps working if the user declines Google Location Accuracy.
            forceLocationManager: true,
          ),
        ).listen(
          _updatePosition,
          onError: (_) => _showProblem('Location is unavailable.'),
        );
  }

  void _showProblem(String message) {
    if (!mounted) return;

    setState(() {
      _problem = message;
    });
  }

  void _updatePosition(Position position) {
    if (!mounted) return;

    final point = LatLng(position.latitude, position.longitude);

    setState(() {
      _current = point;
      _problem = null;
    });

    _follow();
  }

  static HazardAhead? _nearest(List<HazardAhead> hazards) {
    HazardAhead? nearest;

    for (final hazard in hazards) {
      if (nearest == null || hazard.distanceM < nearest.distanceM) {
        nearest = hazard;
      }
    }

    return nearest;
  }

  /// Keeps the car, and the nearest hazard ahead if there is one, on screen.
  void _follow({bool resetZoom = false}) {
    final point = _current;

    if (point == null || !_mapReady || !_following) return;

    final hazard = _nearest(widget.hazards);

    if (hazard == null) {
      _mapController.move(
        point,
        resetZoom ? _zoom : _mapController.camera.zoom,
      );
      return;
    }

    _mapController.fitCamera(
      CameraFit.coordinates(
        coordinates: [point, LatLng(hazard.lat, hazard.lon)],
        padding: _fitPadding,
        maxZoom: _maxFitZoom,
      ),
    );
  }

  Marker _hazardMarker(HazardAhead hazard) {
    final Color colour;

    if (hazard.isAdvisory) {
      colour = const Color(0xFFB8C2CF);
    } else {
      colour = hazard.inAlertRange ? AppColors.coral : const Color(0xFFFFB199);
    }

    return Marker(
      key: ValueKey('hazard-${hazard.id}'),
      point: LatLng(hazard.lat, hazard.lon),
      width: 44,
      height: 44,
      child: IconButton.filled(
        tooltip: hazard.title,
        onPressed: widget.onHazardTap == null
            ? null
            : () => widget.onHazardTap!(hazard),
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

  void _centerOnMe() {
    final point = _current;

    if (point == null || !_mapReady) return;

    setState(() {
      _following = true;
    });

    _follow(resetZoom: true);
  }

  @override
  Widget build(BuildContext context) {
    final point = _current;

    return ClipRRect(
      borderRadius: BorderRadius.circular(24),
      child: Stack(
        fit: StackFit.expand,
        children: [
          FlutterMap(
            mapController: _mapController,
            options: MapOptions(
              initialCenter: _cairo,
              initialZoom: _zoom,
              onMapReady: () {
                _mapReady = true;
                _follow(resetZoom: true);
              },
              onPositionChanged: (camera, hasGesture) {
                if (hasGesture && _following) {
                  setState(() {
                    _following = false;
                  });
                }
              },
            ),
            children: [
              TileLayer(
                urlTemplate: 'https://tile.openstreetmap.org/{z}/{x}/{y}.png',
                userAgentPackageName: 'com.example.rovo_mobile',
              ),
              if (widget.hazards.isNotEmpty)
                MarkerLayer(
                  markers: [for (final h in widget.hazards) _hazardMarker(h)],
                ),
              if (point != null)
                MarkerLayer(
                  markers: [
                    Marker(
                      point: point,
                      width: 44,
                      height: 44,
                      child: Container(
                        decoration: BoxDecoration(
                          color: AppColors.turquoise,
                          shape: BoxShape.circle,
                          border: Border.all(color: Colors.white, width: 3),
                        ),
                        child: const Icon(
                          Icons.navigation,
                          color: AppColors.navy,
                          size: 22,
                        ),
                      ),
                    ),
                  ],
                ),
              const RichAttributionWidget(
                attributions: [
                  TextSourceAttribution('OpenStreetMap contributors'),
                ],
              ),
            ],
          ),
          Positioned(
            left: 16,
            bottom: 16,
            child: FloatingActionButton.small(
              heroTag: 'center-on-me',
              tooltip: 'Show my position',
              backgroundColor: Colors.white,
              foregroundColor: AppColors.navy,
              onPressed: point == null ? null : _centerOnMe,
              child: Icon(
                _following ? Icons.my_location : Icons.location_searching,
              ),
            ),
          ),
          if (_problem != null)
            Positioned(
              top: 12,
              left: 12,
              right: 12,
              child: Container(
                padding: const EdgeInsets.all(12),
                decoration: BoxDecoration(
                  color: Colors.white,
                  borderRadius: BorderRadius.circular(12),
                ),
                child: Text(
                  _problem!,
                  textAlign: TextAlign.center,
                  style: const TextStyle(color: AppColors.navy),
                ),
              ),
            ),
        ],
      ),
    );
  }
}
