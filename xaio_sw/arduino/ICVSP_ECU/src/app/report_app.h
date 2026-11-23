#pragma once
// Report firmware: starts its own FreeRTOS task (big stack) and returns at once.
void start_report_app();
// Debug: one line with the report counters (signed, stored by AWS, waiting to send).
void report_status();
