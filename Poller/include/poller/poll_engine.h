#pragma once

#include "emulator/modbus_types.h"
#include "emulator/register_store.h"
#include "poller/serial_rtu_master.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace poller {

class PollEngine {
public:
    using LogCallback = std::function<void(const std::string& line, bool storeChanged, emulator::TableKind table)>;
    using ErrorCallback = std::function<void(const std::string& message)>;

    PollEngine(emulator::RegisterStore& store, SerialRtuMaster& master);
    ~PollEngine();

    PollEngine(const PollEngine&) = delete;
    PollEngine& operator=(const PollEngine&) = delete;

    void setInterval(std::chrono::milliseconds interval);
    void setTimeout(std::chrono::milliseconds timeout);
    void setCallbacks(LogCallback onLog, ErrorCallback onError);

    void queueWrite(emulator::TableKind kind, std::uint8_t device, std::uint16_t address, std::uint16_t value);

    bool start();
    void requestStop();
    void stop();
    bool running() const;

private:
    struct WriteJob {
        emulator::TableKind kind;
        std::uint8_t device;
        std::uint16_t address;
        std::uint16_t value;
    };

    struct ReadJob {
        emulator::TableKind kind;
        std::uint8_t device;
        std::uint16_t start;
        std::uint16_t quantity;
    };

    void loop();
    void drainWrites();
    void pollReads();
    std::vector<ReadJob> makeJobs(emulator::TableKind kind) const;
    void executeRead(const ReadJob& job);
    void executeWrite(const WriteJob& job);
    void logLine(std::string line, bool changed, emulator::TableKind table);

    emulator::RegisterStore& store_;
    SerialRtuMaster& master_;
    std::atomic<bool> running_{false};
    std::atomic<int> intervalMs_{200};
    std::atomic<int> timeoutMs_{1000};
    LogCallback onLog_;
    ErrorCallback onError_;
    std::mutex writeMutex_;
    std::vector<WriteJob> writes_;
    std::thread thread_;
};

}  // namespace poller
