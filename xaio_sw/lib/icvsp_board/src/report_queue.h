#pragma once
#include <cstddef>
#include <cstdint>

namespace icvsp {

// Signed report lines waiting to be delivered, oldest first, in a fixed block of RAM (no heap use).
// When it is full the oldest report is dropped, like the phone's queue: a fresh hazard matters more
// than an old one the backend may already reject as stale.
//
// RAM only for now: reports are lost on a reboot or power cut. Keeping them across reboots (NVS or a
// flash partition, as in Malak's BLE branch `feat/ble-durable-nvs-queue`) is the next step.
class ReportQueue {
public:
    static constexpr size_t kCapacity = 32;
    static constexpr size_t kMaxLine = 1100;  // Device builds lines up to 1099 characters

    // false if the line is empty or too long (it is not stored).
    bool push(const char* line);
    const char* front() const { return count_ ? slots_[head_] : nullptr; }
    void pop();

    size_t size() const { return count_; }
    bool empty() const { return count_ == 0; }
    uint32_t dropped() const { return dropped_; }  // oldest reports discarded because the queue was full

private:
    char slots_[kCapacity][kMaxLine] = {};
    size_t head_ = 0, count_ = 0;
    uint32_t dropped_ = 0;
};

}  // namespace icvsp
