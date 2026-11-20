#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace emulator {

enum class ParityMode { None, Even, Odd };
enum class StopBitsMode { One, Two };

struct SerialSettings {
    std::string port = "COM1";
    unsigned baud = 9600;
    ParityMode parity = ParityMode::None;
    StopBitsMode stopBits = StopBitsMode::One;
};

class SerialRtuServer {
public:
    using FrameHandler = std::function<std::optional<std::vector<std::uint8_t>>(const std::uint8_t* data, std::size_t length)>;
    using ErrorHandler = std::function<void(const std::string& message)>;

    SerialRtuServer();
    ~SerialRtuServer();

    SerialRtuServer(const SerialRtuServer&) = delete;
    SerialRtuServer& operator=(const SerialRtuServer&) = delete;

    bool start(const SerialSettings& settings, FrameHandler onFrame, ErrorHandler onError, std::string& error);
    void stop();
    bool running() const;

    static std::chrono::microseconds InterFrameTimeout(unsigned baud);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace emulator
