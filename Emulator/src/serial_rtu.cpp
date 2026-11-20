#include "emulator/modbus_crc.h"
#include "emulator/modbus_types.h"
#include "emulator/serial_ports.h"
#include "emulator/serial_rtu.h"

#include <chrono>
#include <cstddef>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/serial_port.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/write.hpp>

namespace emulator {

struct SerialRtuServer::Impl {
    boost::asio::io_context io;
    boost::asio::strand<boost::asio::io_context::executor_type> strand;
    boost::asio::serial_port port;
    boost::asio::steady_timer timer;
    std::thread thread;
    FrameHandler onFrame;
    ErrorHandler onError;
    std::vector<std::uint8_t> rx;
    std::uint8_t readBuf[512]{};
    std::chrono::microseconds t35{1750};
    bool running = false;

    Impl()
        : strand(boost::asio::make_strand(io)),
          port(io),
          timer(io) {}

    void doRead();
    void armSilenceTimer();
    void onSilence();
    void dispatchFrame(const std::uint8_t* data, std::size_t length);
    bool extractReadyFrames();
    std::string openPort(const SerialSettings& settings);
};

SerialRtuServer::SerialRtuServer() : impl_(std::make_unique<Impl>()) {}

SerialRtuServer::~SerialRtuServer() {
    stop();
}

std::chrono::microseconds SerialRtuServer::InterFrameTimeout(unsigned baud) {
    // Spec T3.5 is ~4 ms at 9600, but Windows/USB-CDC often delivers a
    // single ADU in several chunks with larger gaps. A 20 ms floor keeps
    // an 8-byte request together without merging two master polls.
    constexpr auto kUsbFloor = std::chrono::microseconds(20000);
    if (baud == 0) {
        return kUsbFloor;
    }
    const double bitsPerChar = 11.0;
    const double usec = (3.5 * bitsPerChar * 1'000'000.0) / static_cast<double>(baud);
    auto spec = static_cast<long long>(usec + 0.5);
    if (spec < 1750) {
        spec = 1750;
    }
    const auto fromSpec = std::chrono::microseconds(spec);
    return fromSpec > kUsbFloor ? fromSpec : kUsbFloor;
}

bool SerialRtuServer::running() const {
    return impl_ && impl_->running;
}

std::string SerialRtuServer::Impl::openPort(const SerialSettings& settings) {
    const std::string path = WindowsDevicePath(settings.port);
    if (path.empty()) {
        return "COM port is empty";
    }

    boost::system::error_code ec;
    port.open(path, ec);
    if (ec) {
        return "Cannot open " + settings.port + ": " + ec.message();
    }

    port.set_option(boost::asio::serial_port_base::baud_rate(settings.baud), ec);
    if (ec) {
        return "Baud rate failed: " + ec.message();
    }
    port.set_option(boost::asio::serial_port_base::character_size(8), ec);
    port.set_option(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::none), ec);

    boost::asio::serial_port_base::parity::type parity = boost::asio::serial_port_base::parity::none;
    if (settings.parity == ParityMode::Even) {
        parity = boost::asio::serial_port_base::parity::even;
    } else if (settings.parity == ParityMode::Odd) {
        parity = boost::asio::serial_port_base::parity::odd;
    }
    port.set_option(boost::asio::serial_port_base::parity(parity), ec);
    if (ec) {
        return "Parity failed: " + ec.message();
    }

    const auto stop = settings.stopBits == StopBitsMode::Two
        ? boost::asio::serial_port_base::stop_bits::two
        : boost::asio::serial_port_base::stop_bits::one;
    port.set_option(boost::asio::serial_port_base::stop_bits(stop), ec);
    if (ec) {
        return "Stop bits failed: " + ec.message();
    }

    return {};
}

bool SerialRtuServer::start(const SerialSettings& settings, FrameHandler onFrame, ErrorHandler onError, std::string& error) {
    stop();
    impl_->onFrame = std::move(onFrame);
    impl_->onError = std::move(onError);
    impl_->t35 = InterFrameTimeout(settings.baud);
    impl_->rx.clear();
    impl_->rx.reserve(kMaxAduSize);

    error = impl_->openPort(settings);
    if (!error.empty()) {
        boost::system::error_code ec;
        if (impl_->port.is_open()) {
            impl_->port.close(ec);
        }
        return false;
    }

    impl_->running = true;
    impl_->io.restart();
    boost::asio::post(impl_->strand, [this] { impl_->doRead(); });
    impl_->thread = std::thread([this] {
        try {
            impl_->io.run();
        } catch (const std::exception& ex) {
            if (impl_->onError) {
                impl_->onError(ex.what());
            }
        }
        impl_->running = false;
    });
    return true;
}

void SerialRtuServer::stop() {
    if (!impl_ || (!impl_->running && !impl_->thread.joinable())) {
        return;
    }

    boost::system::error_code ec;
    boost::asio::post(impl_->strand, [this] {
        boost::system::error_code inner;
        impl_->timer.cancel();
        if (impl_->port.is_open()) {
            impl_->port.close(inner);
        }
        impl_->io.stop();
    });

    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }

    if (impl_->port.is_open()) {
        impl_->port.close(ec);
    }
    impl_->timer.cancel();
    impl_->rx.clear();
    impl_->running = false;
    impl_->io.restart();
}

void SerialRtuServer::Impl::doRead() {
    if (!port.is_open()) {
        return;
    }
    port.async_read_some(
        boost::asio::buffer(readBuf),
        boost::asio::bind_executor(strand, [this](const boost::system::error_code& ec, std::size_t bytes) {
            if (ec) {
                if (ec != boost::asio::error::operation_aborted && onError) {
                    onError(ec.message());
                }
                return;
            }
            rx.insert(rx.end(), readBuf, readBuf + bytes);
            if (rx.size() > kMaxAduSize * 2) {
                rx.erase(rx.begin(), rx.begin() + static_cast<std::ptrdiff_t>(rx.size() - kMaxAduSize));
            }
            extractReadyFrames();
            armSilenceTimer();
            doRead();
        }));
}

void SerialRtuServer::Impl::armSilenceTimer() {
    timer.expires_after(t35);
    timer.async_wait(boost::asio::bind_executor(strand, [this](const boost::system::error_code& ec) {
        if (ec) {
            return;
        }
        onSilence();
    }));
}

void SerialRtuServer::Impl::dispatchFrame(const std::uint8_t* data, std::size_t length) {
    if (!onFrame || data == nullptr || length < 4) {
        return;
    }
    std::optional<std::vector<std::uint8_t>> reply;
    try {
        reply = onFrame(data, length);
    } catch (const std::exception& ex) {
        if (onError) {
            onError(ex.what());
        }
        return;
    }
    if (!reply || reply->empty() || !port.is_open()) {
        return;
    }
    boost::system::error_code ec;
    boost::asio::write(port, boost::asio::buffer(*reply), ec);
    if (ec && onError && ec != boost::asio::error::operation_aborted) {
        onError(ec.message());
    }
}

bool SerialRtuServer::Impl::extractReadyFrames() {
    bool dispatched = false;
    while (rx.size() >= 4) {
        const std::size_t need = ExpectedRtuAduLength(rx.data(), rx.size());
        if (need == 0 || need > kMaxAduSize) {
            break;
        }
        if (rx.size() < need) {
            break;
        }
        if (FrameCrcIsValid(rx.data(), need)) {
            dispatchFrame(rx.data(), need);
            rx.erase(rx.begin(), rx.begin() + static_cast<std::ptrdiff_t>(need));
            dispatched = true;
            continue;
        }
        // Likely a leftover byte before a real ADU.
        rx.erase(rx.begin());
    }
    return dispatched;
}

void SerialRtuServer::Impl::onSilence() {
    const auto original = rx;
    bool recovered = extractReadyFrames();
    while (rx.size() >= 4 && !FrameCrcIsValid(rx.data(), rx.size())) {
        rx.erase(rx.begin());
        recovered = extractReadyFrames() || recovered;
    }
    if (rx.size() >= 4 && FrameCrcIsValid(rx.data(), rx.size())) {
        dispatchFrame(rx.data(), rx.size());
        rx.clear();
        return;
    }
    if (!recovered && original.size() >= 4) {
        dispatchFrame(original.data(), original.size());
    }
    rx.clear();
}

}  // namespace emulator
