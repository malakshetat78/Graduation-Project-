# T-012 camera bring-up tools

Tools for the T-012 camera test in the ESP-IDF project ([1_XAIO_SW/code](../../../1_XAIO_SW/doc/README.md), component `icvsp_camera`).
Test cases and results: [T-012 camera test cases](../../doc/camera/T-012-camera-test-cases.md).

| File | What it does | Runs on |
| --- | --- | --- |
| [extract_jpegs.py](extract_jpegs.py) | Turns a saved camera test log into `.jpg` files, checks each image (structure, and a full decode if Pillow is installed), checks the run is complete and passed, and prints Markdown result tables | Computer, Python 3 (Pillow optional) |
| [run_host_tests.sh](run_host_tests.sh) | Builds and runs the C++ unit tests for the firmware's JPEG check and Base64 encoder, runs the Python tests, and round-trips a real JPEG through the firmware's log format | Computer (macOS or Linux), needs a C++17 compiler and Python 3 |
| [test_jpeg_check.cpp](test_jpeg_check.cpp) | C++ unit tests for `1_XAIO_SW/code/components/icvsp_camera/jpeg_check.cpp` | Computer |
| [test_extract_jpegs.py](test_extract_jpegs.py) | Python unit tests for `extract_jpegs.py` | Computer |
| [sample_log.cpp](sample_log.cpp) | Prints a `.jpg` the way the firmware prints a sample photo (used by the round trip) | Computer |

## Run

```text
cd 5_TESTING/code/camera
./run_host_tests.sh                 # optional: ./run_host_tests.sh some_photo.jpg
python3 extract_jpegs.py camera_log.txt --out photos
```

`extract_jpegs.py` exits with 0 only if the log holds one complete run that passed: a `CAM RESULT` line with
`fail=0` and as many `CAM` check lines as it counts, no FAIL line, at least one photo, and every photo OK. A
broken photo block, a cut log, two runs in one file or a failed run exits 1 and prints why. A run with the camera
unplugged (TC-CAM-10) is expected to exit 1. Save one run per file and copy the whole monitor output.

With Pillow installed every photo is also decoded. Pillow rejects broken headers and tables, but it fills in
missing image data without an error, so opening the photos and looking at them is still required (TC-CAM-06).

## What these results mean

Host results only check the code around the camera: the JPEG check, the Base64 printing, the log
extraction and the behaviour when no camera answers. They are **not camera evidence**. Camera startup, image
quality, timing and memory use are only shown by runs on the physical XIAO ESP32-S3 Sense, recorded in the test
cases with setup "physical board".
