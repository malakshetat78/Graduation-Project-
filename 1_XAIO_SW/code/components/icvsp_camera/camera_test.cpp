// T-012 camera bring-up test: startup, sensor identity, repeated capture at several resolutions, image
// validity, capture timing, memory use, capture failures and recovery. ESP-IDF build (CONFIG_ICVSP_APP_CAMERA).
// Output lines (all start at the beginning of a line so the log can be parsed):
//   CAM <check> PASS|FAIL <details>         one per check
//   CAMSTAT key=value ...                   measurements, one line per resolution and per memory snapshot
//   -----BEGIN ICVSP JPEG ...----- / -----END ICVSP JPEG-----   sample image as Base64
//   CAM RESULT pass=N fail=M                last line
// This test only shows that the camera produces valid images. It does not measure hazard-detection accuracy.
#include "camera_test.h"

#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "esp_app_desc.h"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "jpeg_check.h"
#include "sdkconfig.h"
#include "sense_camera.h"

namespace {

#ifdef CONFIG_ICVSP_CAM_DUMP_JPEG
constexpr bool kDumpJpeg = true;
#else
constexpr bool kDumpJpeg = false;
#endif

// Every line goes to the console in one piece, so ESP-IDF driver messages cannot cut into it.
void log_line(const char* format, ...) __attribute__((format(printf, 1, 2)));

void log_line(const char* format, ...)
{
    char text[768];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    fputs(text, stdout);
    fflush(stdout);
}

void step(int n, int total, const char* what) { log_line("\n>>> STEP %d/%d: %s\n", n, total, what); }

int g_pass = 0;
int g_fail = 0;

void result(const char* name, bool ok, const char* format, ...) __attribute__((format(printf, 3, 4)));

void result(const char* name, bool ok, const char* format, ...)
{
    char detail[384];
    va_list args;
    va_start(args, format);
    vsnprintf(detail, sizeof detail, format, args);
    va_end(args);
    log_line("CAM %-14s %s  %s\n", name, ok ? "PASS" : "FAIL", detail);
    ok ? ++g_pass : ++g_fail;
}

int64_t now_us() { return esp_timer_get_time(); }

struct Memory {
    size_t internal_free;
    size_t internal_largest;
    size_t psram_free;
    size_t heap_min_ever;
};

Memory memory_now()
{
    return Memory{heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                  heap_caps_get_free_size(MALLOC_CAP_SPIRAM), esp_get_minimum_free_heap_size()};
}

void print_memory(const char* when, const Memory& m)
{
    log_line("CAMSTAT mem when=%s internal_free=%zu internal_largest=%zu psram_free=%zu heap_min_ever=%zu\n", when,
             m.internal_free, m.internal_largest, m.psram_free, m.heap_min_ever);
}

const char* sensor_name(const sensor_t* sensor)
{
    if (sensor == nullptr) {
        return "none";
    }
    sensor_id_t id = sensor->id;
    const camera_sensor_info_t* info = esp_camera_sensor_get_info(&id);
    return info ? info->name : "unknown";
}

struct Resolution {
    const char* name;
    framesize_t size;
    uint16_t width;
    uint16_t height;
    size_t fb_count;  // frame buffers
};

// The AI model input and the two next sizes up. All fit the OV3660 (max 2048x1536).
// SVGA gets two frame buffers: with one, the driver dropped frames (cam_hal: FB-OVF) and some buffers held two or
// four pictures stuck together (ESP-IDF run 5 and Arduino run 1, 2026-11-24). With two, the camera can fill one
// buffer while the test reads the other.
constexpr Resolution kResolutions[] = {
    {"QVGA", FRAMESIZE_QVGA, 320, 240, 1},
    {"VGA", FRAMESIZE_VGA, 640, 480, 1},
    {"SVGA", FRAMESIZE_SVGA, 800, 600, 2},
};

// Starts the camera; on failure deinitializes and retries. Returns the number of attempts used, 0 if all failed.
int start_camera(const icvsp::SenseCameraOptions& options, int attempts, int64_t* init_us, esp_err_t* last_error)
{
    const camera_config_t config = icvsp::sense_camera_config(options);
    for (int attempt = 1; attempt <= attempts; ++attempt) {
        const int64_t t0 = now_us();
        const esp_err_t status = esp_camera_init(&config);
        *init_us = now_us() - t0;
        *last_error = status;
        if (status == ESP_OK) {
            return attempt;
        }
        log_line("CAM init attempt %d/%d failed: %s (0x%x)\n", attempt, attempts, esp_err_to_name(status),
                 static_cast<unsigned>(status));
        esp_camera_deinit();  // safe if init stopped half way
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    return 0;
}

// Returns false if the photo could not be printed (no memory for the Base64 copy).
bool dump_jpeg(const char* label, const camera_fb_t* frame)
{
    const size_t b64_len = icvsp::base64_size(frame->len);
    char* text = static_cast<char*>(heap_caps_malloc(b64_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (text == nullptr) {
        log_line("CAM dump %s skipped: no memory for %zu bytes of Base64, trying the next photo\n", label, b64_len);
        return false;
    }
    icvsp::base64_encode(frame->buf, frame->len, text, b64_len + 1);
    log_line("-----BEGIN ICVSP JPEG name=%s width=%zu height=%zu bytes=%zu lighting=\"%s\"-----\n", label,
             frame->width, frame->height, frame->len, CONFIG_ICVSP_CAM_LIGHTING);
    constexpr size_t kLine = 76;
    for (size_t pos = 0; pos < b64_len; pos += kLine) {
        const size_t n = b64_len - pos < kLine ? b64_len - pos : kLine;
        log_line("%.*s\n", static_cast<int>(n), text + pos);
    }
    log_line("-----END ICVSP JPEG-----\n");
    heap_caps_free(text);
    return true;
}

// Repeated capture at one resolution. Returns true if the camera started.
bool capture_series(const Resolution& res, int shots, bool dump)
{
    icvsp::SenseCameraOptions options;
    options.frame_size = res.size;
    options.fb_count = res.fb_count;
    options.fb_location = esp_psram_get_size() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;

    char check[24];
    snprintf(check, sizeof check, "start_%s", res.name);
    int64_t init_us = 0;
    esp_err_t error = ESP_OK;
    const int attempts = start_camera(options, CONFIG_ICVSP_CAM_INIT_ATTEMPTS, &init_us, &error);
    if (attempts == 0) {
        result(check, false, "camera did not start after %d attempts: %s. Board off, then check the camera ribbon",
               CONFIG_ICVSP_CAM_INIT_ATTEMPTS, esp_err_to_name(error));
        return false;
    }
    result(check, true, "init %.1f ms (attempt %d)", init_us / 1000.0, attempts);

    // Warm-up: the first frames after start can be dark or still adjusting exposure.
    for (int i = 0; i < CONFIG_ICVSP_CAM_WARMUP_FRAMES; ++i) {
        camera_fb_t* frame = esp_camera_fb_get();
        if (frame) {
            esp_camera_fb_return(frame);
        }
    }

    int ok = 0;
    int null_frames = 0;
    int bad_jpeg = 0;
    int stuck = 0;  // part of bad_jpeg: more than one picture in the buffer, and the first one not usable
    int cut = 0;    // buffers that held more than one picture and were cut to the first (usable) picture
    int wrong_size = 0;
    int too_small = 0;
    int64_t get_min = INT64_MAX, get_max = 0, get_sum = 0;
    int64_t gap_sum = 0;
    int gap_count = 0;
    size_t bytes_min = SIZE_MAX, bytes_max = 0, bytes_sum = 0;
    int64_t last_frame_at = 0;
    bool dumped = !dump;
    Memory during{};
    bool during_measured = false;

    for (int i = 0; i < shots; ++i) {
        const int64_t t0 = now_us();
        bool was_cut = false;
        camera_fb_t* frame = icvsp::get_photo(&was_cut);
        const int64_t t1 = now_us();
        if (frame == nullptr) {
            ++null_frames;
            log_line("CAM %s shot %d: no frame (capture timeout)\n", res.name, i + 1);
            continue;
        }
        const int64_t get_us = t1 - t0;
        get_min = get_us < get_min ? get_us : get_min;
        get_max = get_us > get_max ? get_us : get_max;
        get_sum += get_us;
        if (last_frame_at != 0) {
            gap_sum += t1 - last_frame_at;
            ++gap_count;
        }
        last_frame_at = t1;

        cut += was_cut ? 1 : 0;
        const icvsp::JpegInfo info = icvsp::inspect_jpeg(frame->buf, frame->len);
        const bool size_ok = info.width == res.width && info.height == res.height && frame->width == res.width &&
                             frame->height == res.height;
        const bool big_enough = frame->len >= CONFIG_ICVSP_CAM_MIN_JPEG_BYTES;
        if (!info.valid()) {
            ++bad_jpeg;
            if (info.stray_marker_offset != 0) {
                ++stuck;
            }
        } else if (!size_ok) {
            ++wrong_size;
        } else if (!big_enough) {
            ++too_small;
        } else {
            ++ok;
        }
#ifdef CONFIG_ICVSP_CAM_PRINT_EVERY_SHOT
        log_line("CAM %s shot %2d: %zu bytes, %ux%u, start=%d end=%d one_picture=%d%s, wait %.1f ms\n",
                 res.name, i + 1, frame->len, info.width, info.height, info.has_start, info.has_end,
                 info.stray_marker_offset == 0, was_cut ? " (cut: 2+ pictures in the buffer, first kept)" : "",
                 get_us / 1000.0);
#endif
        bytes_min = frame->len < bytes_min ? frame->len : bytes_min;
        bytes_max = frame->len > bytes_max ? frame->len : bytes_max;
        bytes_sum += frame->len;

        if (!during_measured && i >= shots / 2) {
            during = memory_now();  // camera running, one frame held (first frame from the middle of the series)
            during_measured = true;
        }
        if (!dumped && info.valid() && size_ok) {
            char label[32];
            snprintf(label, sizeof label, "%s_shot%02d", res.name, i + 1);
            dumped = dump_jpeg(label, frame);
            last_frame_at = 0;  // printing takes seconds: do not count it as a frame gap
        }
        esp_camera_fb_return(frame);
    }

    const sensor_t* sensor = esp_camera_sensor_get();
    const int got = shots - null_frames;
    log_line("CAMSTAT res=%s width=%u height=%u fb_count=%zu shots=%d valid=%d no_frame=%d bad_jpeg=%d stuck=%d cut=%d "
             "wrong_size=%d too_small=%d "
             "init_ms=%.1f get_ms_min=%.1f get_ms_avg=%.1f get_ms_max=%.1f frame_gap_ms_avg=%.1f fps=%.1f "
             "bytes_min=%zu bytes_avg=%zu bytes_max=%zu sensor=%s lighting=\"%s\"\n",
             res.name, res.width, res.height, res.fb_count, shots, ok, null_frames, bad_jpeg, stuck, cut, wrong_size, too_small,
             init_us / 1000.0,
             got ? get_min / 1000.0 : 0.0, got ? get_sum / 1000.0 / got : 0.0, get_max / 1000.0,
             gap_count ? gap_sum / 1000.0 / gap_count : 0.0, gap_sum ? 1e6 * gap_count / gap_sum : 0.0,
             got ? bytes_min : 0, got ? bytes_sum / got : 0, bytes_max, sensor_name(sensor), CONFIG_ICVSP_CAM_LIGHTING);
    char mem_label[24];
    snprintf(mem_label, sizeof mem_label, "running_%s", res.name);
    if (during_measured) {
        print_memory(mem_label, during);
    } else {
        log_line("CAMSTAT mem when=%s not_measured=1 (no frame in the second half of the series)\n", mem_label);
    }

    snprintf(check, sizeof check, "capture_%s", res.name);
    char cut_note[96] = "";
    if (cut) {
        snprintf(cut_note, sizeof cut_note, ", %d buffer(s) held 2+ pictures and were cut to the first", cut);
    }
    result(check, ok == shots && dumped, "%d/%d valid %ux%u JPEGs, wait avg %.1f ms, %zu bytes avg%s%s%s", ok, shots,
           res.width, res.height, got ? get_sum / 1000.0 / got : 0.0, got ? bytes_sum / got : 0, cut_note,
           stuck ? ", some buffers held broken pictures" : "",
           dumped ? "" : ", sample photo NOT printed (evidence missing)");

    esp_camera_deinit();
    return true;
}

// F1: asking for a frame while the camera is stopped must return no frame, not crash.
void failure_capture_while_stopped()
{
    const camera_fb_t* frame = esp_camera_fb_get();
    result("fail_stopped", frame == nullptr, "capture with camera stopped returned %s",
           frame == nullptr ? "no frame, handled" : "a frame (unexpected)");
}

// F2: a start that cannot work (largest frame buffer forced into internal RAM) must fail with an error code,
// then the camera must start again normally.
void failure_init_then_recover(const Memory& baseline, bool camera_found)
{
    if (!camera_found) {
        // Without a working camera these two cases cannot show anything: the start fails before the
        // frame buffer is even requested. Report them as not run instead of a misleading PASS.
        result("fail_init", false, "not run: needs a camera that starts (fix the startup check first)");
        result("recover", false, "not run: needs a camera that starts");
        return;
    }
    icvsp::SenseCameraOptions impossible;
    impossible.frame_size = FRAMESIZE_QXGA;  // 2048x1536 -> about 630 KB buffer, more than the internal RAM
    impossible.fb_location = CAMERA_FB_IN_DRAM;
    // esp32-camera sizes a JPEG frame buffer as width * height / 5 and allocates it in esp_camera_init's
    // config step, which returns ESP_FAIL when the allocation fails. Sensor and bus problems return other codes.
    constexpr size_t kNeeded = 2048u * 1536u / 5u;
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    int64_t init_us = 0;
    esp_err_t error = ESP_OK;
    const int attempts = start_camera(impossible, 1, &init_us, &error);
    if (attempts != 0) {
        result("fail_init", false, "start with a 2048x1536 buffer in internal RAM unexpectedly worked");
        esp_camera_deinit();
    } else {
        // Counts only if the error is the one the buffer allocation gives (ESP_FAIL) and the buffer really could
        // not fit. Any other code (sensor, SCCB, peripheral) means the start failed for an unrelated reason.
        const bool memory_cannot_fit = largest < kNeeded;
        const bool for_the_right_reason = error == ESP_FAIL && memory_cannot_fit;
        result("fail_init", for_the_right_reason,
               "start refused: %s (0x%x) after %.1f ms, largest internal block %zu < needed %zu: %s",
               esp_err_to_name(error), static_cast<unsigned>(error), init_us / 1000.0, largest, kNeeded,
               for_the_right_reason ? "frame buffer refused, as expected"
               : (memory_cannot_fit ? "unexpected error code, the start failed for another reason"
                                    : "the buffer would fit, so this test cannot force the failure"));
    }

    icvsp::SenseCameraOptions normal;
    normal.fb_location = esp_psram_get_size() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
    const int recovered = start_camera(normal, CONFIG_ICVSP_CAM_INIT_ATTEMPTS, &init_us, &error);
    bool frame_ok = false;
    size_t frame_len = 0;
    if (recovered) {
        for (int i = 0; i < CONFIG_ICVSP_CAM_WARMUP_FRAMES; ++i) {  // warm-up frames are not judged
            camera_fb_t* frame = esp_camera_fb_get();
            if (frame) {
                esp_camera_fb_return(frame);
            }
        }
        bool was_cut = false;
        camera_fb_t* frame = icvsp::get_photo(&was_cut);  // the frame that counts
        if (frame) {
            const icvsp::JpegInfo info = icvsp::inspect_jpeg(frame->buf, frame->len);
            frame_ok = info.valid() && info.width == 320 && info.height == 240 && frame->width == 320 &&
                       frame->height == 240 && frame->len >= CONFIG_ICVSP_CAM_MIN_JPEG_BYTES;
            frame_len = frame->len;
            esp_camera_fb_return(frame);
        }
        esp_camera_deinit();
    }
    result("recover", recovered && frame_ok, "camera %s after the failed start, frame %s (%zu bytes)",
           recovered ? "started again" : "did NOT start", frame_ok ? "valid 320x240" : "missing or INVALID", frame_len);
}

// The camera allocates its buffers on start and must give all of them back on stop, also after failed starts.
void check_memory_back(const Memory& baseline)
{
    const Memory after = memory_now();
    print_memory("after_all_tests", after);
    const long internal_lost = static_cast<long>(baseline.internal_free) - static_cast<long>(after.internal_free);
    const long psram_lost = static_cast<long>(baseline.psram_free) - static_cast<long>(after.psram_free);
    result("memory_back", internal_lost <= CONFIG_ICVSP_CAM_LEAK_TOLERANCE && psram_lost <= CONFIG_ICVSP_CAM_LEAK_TOLERANCE,
           "after all tests: internal %ld bytes, PSRAM %ld bytes less than before the camera started (limit %d)",
           internal_lost, psram_lost, CONFIG_ICVSP_CAM_LEAK_TOLERANCE);
}

}  // namespace

int run_camera_test()
{
    g_pass = 0;
    g_fail = 0;
    constexpr int kSteps = 6;
    const esp_app_desc_t* app = esp_app_get_description();

    log_line("\nCAM BEGIN T-012 camera bring-up test (ESP-IDF build)\n");
    // firmware= is the git commit the build was made from (a -dirty ending means uncommitted changes).
    log_line("CAMSTAT build firmware=%s idf=%s compiled=\"%s %s\" shots=%d lighting=\"%s\"\n", app->version,
             app->idf_ver, app->date, app->time, CONFIG_ICVSP_CAM_SHOTS, CONFIG_ICVSP_CAM_LIGHTING);

    step(1, kSteps, "memory before the camera starts");
    const size_t psram = esp_psram_get_size();
    result("psram", psram > 0, "%zu KB%s", psram / 1024, psram ? "" : ": frame buffers fall back to internal RAM");
    const Memory baseline = memory_now();
    print_memory("before_camera", baseline);

    step(2, kSteps, "start the camera and read the sensor model");
    icvsp::SenseCameraOptions first;
    first.fb_location = psram ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
    int64_t init_us = 0;
    esp_err_t error = ESP_OK;
    const int attempts = start_camera(first, CONFIG_ICVSP_CAM_INIT_ATTEMPTS, &init_us, &error);
    if (attempts == 0) {
        result("startup", false, "camera did not start after %d attempts: %s (0x%x). Unplug the board, reseat the "
               "camera ribbon, try again", CONFIG_ICVSP_CAM_INIT_ATTEMPTS, esp_err_to_name(error),
               static_cast<unsigned>(error));
    } else {
        const sensor_t* sensor = esp_camera_sensor_get();
        // The camera keeps being tested after a late start, but startup only passes on the first attempt.
        result("startup", attempts == 1, "init %.1f ms (attempt %d)%s", init_us / 1000.0, attempts,
               attempts == 1 ? "" : ": started only after retries, check the camera ribbon");
        result("sensor", sensor != nullptr && sensor->id.PID == OV3660_PID,
               "%s PID=0x%04x VER=0x%02x MIDH=0x%02x MIDL=0x%02x (expected OV3660)", sensor_name(sensor),
               sensor ? sensor->id.PID : 0, sensor ? sensor->id.VER : 0, sensor ? sensor->id.MIDH : 0,
               sensor ? sensor->id.MIDL : 0);
        esp_camera_deinit();
    }

    step(3, kSteps, "repeated capture at QVGA, VGA and SVGA (keep the scene and light the same)");
    if (attempts != 0) {
        for (const Resolution& res : kResolutions) {
            const bool dump = kDumpJpeg && res.size != FRAMESIZE_SVGA;
            capture_series(res, CONFIG_ICVSP_CAM_SHOTS, dump);
        }
    } else {
        result("capture", false, "skipped: the camera did not start");
    }

    step(4, kSteps, "memory after the camera is stopped");
    print_memory("after_capture", memory_now());

    step(5, kSteps, "failure cases and recovery");
    failure_capture_while_stopped();
    failure_init_then_recover(baseline, attempts != 0);
    check_memory_back(baseline);

    step(6, kSteps, g_fail == 0 ? "finished: all passed" : "finished: something FAILED (see the FAIL lines)");
    log_line("CAM RESULT pass=%d fail=%d\n", g_pass, g_fail);
    log_line("%s\n", g_fail == 0 ? "LED: slow blink = all passed" : "LED: fast blink = check the FAIL lines");
    return g_fail;
}
