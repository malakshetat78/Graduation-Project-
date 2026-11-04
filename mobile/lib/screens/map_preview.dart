import 'package:flutter/material.dart';

import '../core/app_colors.dart';

class MapPreview extends StatelessWidget {
  final VoidCallback onHazardTap;

  const MapPreview({super.key, required this.onHazardTap});

  @override
  Widget build(BuildContext context) {
    return ClipRRect(
      borderRadius: BorderRadius.circular(24),
      child: Stack(
        fit: StackFit.expand,
        children: [
          const CustomPaint(painter: _MapPainter()),

          const Align(
            alignment: Alignment(-0.65, -0.45),
            child: Text(
              'ZAMALEK',
              style: TextStyle(
                color: AppColors.navy,
                fontSize: 11,
                fontWeight: FontWeight.w600,
              ),
            ),
          ),

          const Align(
            alignment: Alignment(0.05, -0.05),
            child: Text(
              'CAIRO',
              style: TextStyle(
                color: AppColors.navy,
                fontSize: 19,
                fontWeight: FontWeight.bold,
              ),
            ),
          ),

          const Align(
            alignment: Alignment(0.6, 0.65),
            child: Text(
              'MAADI',
              style: TextStyle(
                color: AppColors.navy,
                fontSize: 11,
                fontWeight: FontWeight.w600,
              ),
            ),
          ),

          Align(
            alignment: const Alignment(0.05, 0.4),
            child: Container(
              padding: const EdgeInsets.all(10),
              decoration: BoxDecoration(
                color: const Color(0xFF008D91),
                shape: BoxShape.circle,
                border: Border.all(color: Colors.white, width: 3),
              ),
              child: const Icon(
                Icons.navigation_rounded,
                color: Colors.white,
                size: 28,
              ),
            ),
          ),

          Align(
            alignment: const Alignment(0.4, -0.35),
            child: IconButton.filled(
              tooltip: 'Preview pothole alert',
              onPressed: onHazardTap,
              style: IconButton.styleFrom(
                backgroundColor: AppColors.coral,
                foregroundColor: AppColors.navy,
              ),
              icon: const Icon(Icons.warning_amber_rounded, size: 30),
            ),
          ),

          Positioned(
            top: 14,
            left: 14,
            child: Container(
              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
              decoration: BoxDecoration(
                color: Colors.white,
                borderRadius: BorderRadius.circular(20),
              ),
              child: const Text(
                'Illustrative map',
                style: TextStyle(color: AppColors.navy, fontSize: 12),
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _MapPainter extends CustomPainter {
  const _MapPainter();

  @override
  void paint(Canvas canvas, Size size) {
    final w = size.width;
    final h = size.height;

    canvas.drawRect(
      Offset.zero & size,
      Paint()..color = const Color(0xFFE8EDE4),
    );

    final parkPaint = Paint()..color = const Color(0xFFCEE3C8);

    canvas.drawRRect(
      RRect.fromRectAndRadius(
        Rect.fromLTWH(w * 0.62, h * 0.52, w * 0.3, h * 0.25),
        const Radius.circular(18),
      ),
      parkPaint,
    );

    final streetPaint = Paint()
      ..color = Colors.white
      ..strokeWidth = 6;

    for (int i = -2; i < 12; i++) {
      canvas.drawLine(
        Offset(i * w / 7, 0),
        Offset(i * w / 7 + w * 0.25, h),
        streetPaint,
      );

      canvas.drawLine(
        Offset(0, i * h / 8),
        Offset(w, i * h / 8 - h * 0.2),
        streetPaint,
      );
    }

    final river = Path()
      ..moveTo(w * 0.3, -20)
      ..cubicTo(w * 0.55, h * 0.25, w * 0.05, h * 0.55, w * 0.25, h + 20);

    canvas.drawPath(
      river,
      Paint()
        ..color = const Color(0xFFA6D8E8)
        ..style = PaintingStyle.stroke
        ..strokeWidth = w * 0.13,
    );

    final route = Path()
      ..moveTo(w * 0.525, h * 0.7)
      ..lineTo(w * 0.525, h * 0.52)
      ..lineTo(w * 0.7, h * 0.43)
      ..lineTo(w * 0.7, h * 0.325);

    canvas.drawPath(
      route,
      Paint()
        ..color = Colors.white
        ..style = PaintingStyle.stroke
        ..strokeWidth = 10
        ..strokeJoin = StrokeJoin.round,
    );

    canvas.drawPath(
      route,
      Paint()
        ..color = const Color(0xFF00B6A5)
        ..style = PaintingStyle.stroke
        ..strokeWidth = 5
        ..strokeJoin = StrokeJoin.round,
    );
  }

  @override
  bool shouldRepaint(covariant _MapPainter oldDelegate) => false;
}
