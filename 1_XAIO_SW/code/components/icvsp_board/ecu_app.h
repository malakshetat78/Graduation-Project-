#pragma once
#include <cstdint>

#include "device.h"
#include "imu_source.h"
#include "nmea.h"
#include "report_queue.h"

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
// Sending is not done here. Signed lines go to the Device's sink (the ReportQueue in the tests, a queue
// to the delivery task on the board), and a separate delivery loop keeps the network link up (WifiLink)
// and sends them (Uplink). So a slow or failing network never delays the IMU reads.
class EcuApp {
public:
    enum class State { WaitingForTime, Running };

    static constexpr uint32_t kMaxSampleAgeMs = 200;  // ~6 m at 100 km/h; position error stays small

    // Give this as the Device's sink with the queue as its context.
    static void queue_sink(const char* line, void* queue);

    EcuApp(Device& device, NmeaParser& gnss, ImuSource& imu) : device_(device), gnss_(gnss), imu_(imu) {}

    void tick(uint32_t now_ms);

    State state() const { return state_; }
    uint32_t imu_samples() const { return imu_samples_; }
    uint32_t imu_discarded() const { return imu_discarded_; }  // read before GNSS time was known
    uint32_t imu_stale() const { return imu_stale_; }  // older than kMaxSampleAgeMs when read
    int64_t last_sample_utc_ms() const { return last_sample_utc_ms_; }  // UTC given to the last used sample

private:
    Device& device_;
    NmeaParser& gnss_;
    ImuSource& imu_;
    State state_ = State::WaitingForTime;
    uint32_t imu_samples_ = 0, imu_discarded_ = 0, imu_stale_ = 0;
    uint32_t running_since_ms_ = 0;
    int64_t last_sample_utc_ms_ = 0;
};

const char* to_string(EcuApp::State s);

}  // namespace icvsp
