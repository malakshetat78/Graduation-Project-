#pragma once
// T-012 camera bring-up test. Prints CAM / CAMSTAT lines and two sample photos as Base64 on the console;
// 5_TESTING/code/camera/extract_jpegs.py turns the saved log into photos and a summary.

// Runs every check once. Returns the number of failed checks (0 = all passed).
int run_camera_test();
