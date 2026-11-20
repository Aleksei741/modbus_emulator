#pragma once

#include "emulator/serial_rtu.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace poller {

class SerialRtuMaster {
public:
    using ErrorHandler = std::function<void(const std::string& message)>;

    enum class Status {
        Ok,
        Timeout,
        BadCrc,
        Transport
    };

    struct Result {
        Status status = Status::Timeout;
        std::vector<std::uint8_t> frame;
        std::string message;
    };

    SerialRtuMaster();
    ~SerialRtuMaster();

    SerialRtuMaster(const SerialRtuMaster&) = delete;
    SerialRtuMaster& operator=(const SerialRtuMaster&) = delete;

    bool start(const emulator::SerialSettings& settings, ErrorHandler onError, std::string& error);
    void stop();
    bool running() const;

    Result transact(const std::uint8_t* request, std::size_t length, std::chrono::milliseconds timeout);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace poller
