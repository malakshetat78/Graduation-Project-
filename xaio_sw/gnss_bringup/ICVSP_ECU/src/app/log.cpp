#include "log.h"

#include <Arduino.h>
#include <cstdarg>
#include <cstdio>

#include "../../config.h"

static SemaphoreHandle_t g_log_lock = nullptr;

static char g_step[128] = "not started yet";
static uint32_t g_step_since_ms = 0;

void log_init() {
    if (!g_log_lock) g_log_lock = xSemaphoreCreateMutex();
}

int log_line(const char* fmt, ...) {
    char buf[768];
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (g_log_lock) xSemaphoreTake(g_log_lock, portMAX_DELAY);
    Serial.print(buf);
    if (g_log_lock) xSemaphoreGive(g_log_lock);
    return n;
}

void step(int n, int total, const char* what) {
    if (g_log_lock) xSemaphoreTake(g_log_lock, portMAX_DELAY);
    snprintf(g_step, sizeof g_step, "STEP %d/%d: %s", n, total, what);
    g_step_since_ms = millis();
#if ICVSP_DEBUG
    Serial.printf("\n>>> %s\n", g_step);
#endif
    if (g_log_lock) xSemaphoreGive(g_log_lock);
}

void where_am_i() {
#if !ICVSP_DEBUG
    return;
#endif
    if (g_log_lock) xSemaphoreTake(g_log_lock, portMAX_DELAY);
    const uint32_t now = millis();
    Serial.printf("[NOW] %s (in this step %lu s, board on %lu s, free RAM %lu KB)\n", g_step,
                  (unsigned long)((now - g_step_since_ms) / 1000), (unsigned long)(now / 1000),
                  (unsigned long)(ESP.getFreeHeap() / 1024));
    if (g_log_lock) xSemaphoreGive(g_log_lock);
}
