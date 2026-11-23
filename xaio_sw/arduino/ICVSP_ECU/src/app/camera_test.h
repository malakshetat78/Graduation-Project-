#pragma once
// T-012 camera bring-up test for the XIAO ESP32-S3 Sense camera.
// Prints CAM <check> PASS/FAIL lines, CAMSTAT measurement lines and, if enabled in config.h, sample JPEGs
// as Base64 blocks that 5_TESTING/code/xiao_camera/extract_jpegs.py turns back into .jpg files.
void run_camera_test();
int camera_failures();
