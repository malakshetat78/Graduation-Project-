#pragma once
#include <cstdint>

#include "device.h"
#include "imu_source.h"
#include "nmea.h"
#include "report_queue.h"
#include "uplink.h"
#include "wifi_link.h"

namespace icvsp {

// The ECU's main loop as a state machine, with every piece of hardware behind an interface so the
// whole thing runs in the native tests:
//
//   WaitingForTime  GNSS has not given a trusted UTC time yet. IMU samples are read and thrown away:
//                   a report without a capture time is useless (the backend checks freshness).
//   Running         IMU samples + the current GNSS fix go to the Device (detect -> fuse -> sign);
//                   each signed line lands in the ReportQueue. A sample is used only if it was taken
//                   after GNSS time became known and is at most kMaxSampleAgeMs old when read (a sample held
//                   up by a slow loop would otherwise get the wrong place and time); its UTC is moved back
//                   by its age.
//
// In both states Wi-Fi is kept up and queued reports are delivered whenever it is online; nothing is lost
// while offline (up to the queue size). Delivery runs inside tick(): a transport that blocks (a real HTTPS
// request waiting on a timeout) stalls the IMU reads for that long. On the board, delivery must move to its
// own task before this is used with live sensors (planned, not done).
class EcuApp {
public:
    enum class State { WaitingForTime, Running };

    static constexpr uint32_t kMaxSampleAgeMs = 200;  // ~6 m at 100 km/h; position error stays small

    // Give this as the Device's sink with the queue as its context.
    static void queue_sink(const char* line, void* queue);

    EcuApp(Device& device, NmeaParser& gnss, ImuSource& imu, WifiLink& wifi, Uplink& uplink, ReportQueue& queue)
        : device_(device), gnss_(gnss), imu_(imu), wifi_(wifi), uplink_(uplink), queue_(queue) {}

    void tick(uint32_t now_ms);

    State state() const { return state_; }
    size_t queued() const { return queue_.size(); }
    uint32_t imu_samples() const { return imu_samples_; }
    uint32_t imu_discarded() const { return imu_discarded_; }  // read before GNSS time was known
    uint32_t imu_stale() const { return imu_stale_; }  // older than kMaxSampleAgeMs when read
    int64_t last_sample_utc_ms() const { return last_sample_utc_ms_; }  // UTC given to the last used sample

private:
    Device& device_;
    NmeaParser& gnss_;
    ImuSource& imu_;
    WifiLink& wifi_;
    Uplink& uplink_;
    ReportQueue& queue_;
    State state_ = State::WaitingForTime;
    uint32_t imu_samples_ = 0, imu_discarded_ = 0, imu_stale_ = 0;
    uint32_t running_since_ms_ = 0;
    int64_t last_sample_utc_ms_ = 0;
};

const char* to_string(EcuApp::State s);

}  // namespace icvsp
