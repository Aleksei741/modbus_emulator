#include "poller/serial_rtu_master.h"

#include "emulator/modbus_crc.h"
#include "emulator/modbus_types.h"
#include "emulator/serial_ports.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
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

namespace poller {

struct SerialRtuMaster::Impl {
    boost::asio::io_context io;
    boost::asio::strand<boost::asio::io_context::executor_type> strand;
    boost::asio::serial_port port;
    boost::asio::steady_timer silenceTimer;
    std::thread thread;
    ErrorHandler onError;
    std::vector<std::uint8_t> rx;
    std::uint8_t readBuf[512]{};
    std::chrono::microseconds t35{1750};
    bool running = false;

    std::mutex mutex;
    std::condition_variable cv;
    bool waiting = false;
    bool done = false;
    std::vector<std::uint8_t> response;

    Impl()
        : strand(boost::asio::make_strand(io)),
          port(io),
          silenceTimer(io) {}

    void doRead();
    void armSilenceTimer();
    void onSilence();
    std::string openPort(const emulator::SerialSettings& settings);
};

SerialRtuMaster::SerialRtuMaster() : impl_(std::make_unique<Impl>()) {}

SerialRtuMaster::~SerialRtuMaster() {
    stop();
}

bool SerialRtuMaster::running() const {
    return impl_ && impl_->running;
}

std::string SerialRtuMaster::Impl::openPort(const emulator::SerialSettings& settings) {
    const std::string path = emulator::WindowsDevicePath(settings.port);
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
    if (settings.parity == emulator::ParityMode::Even) {
        parity = boost::asio::serial_port_base::parity::even;
    } else if (settings.parity == emulator::ParityMode::Odd) {
        parity = boost::asio::serial_port_base::parity::odd;
    }
    port.set_option(boost::asio::serial_port_base::parity(parity), ec);
    if (ec) {
        return "Parity failed: " + ec.message();
    }

    const auto stop = settings.stopBits == emulator::StopBitsMode::Two
        ? boost::asio::serial_port_base::stop_bits::two
        : boost::asio::serial_port_base::stop_bits::one;
    port.set_option(boost::asio::serial_port_base::stop_bits(stop), ec);
    if (ec) {
        return "Stop bits failed: " + ec.message();
    }

    return {};
}

bool SerialRtuMaster::start(const emulator::SerialSettings& settings, ErrorHandler onError, std::string& error) {
    stop();
    impl_->onError = std::move(onError);
    impl_->t35 = emulator::SerialRtuServer::InterFrameTimeout(settings.baud);
    impl_->rx.clear();
    impl_->rx.reserve(emulator::kMaxAduSize);

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
        {
            std::lock_guard lock(impl_->mutex);
            impl_->done = true;
        }
        impl_->cv.notify_all();
    });
    return true;
}

void SerialRtuMaster::stop() {
    if (!impl_ || (!impl_->running && !impl_->thread.joinable())) {
        return;
    }

    boost::system::error_code ec;
    boost::asio::post(impl_->strand, [this] {
        boost::system::error_code inner;
        impl_->silenceTimer.cancel();
        if (impl_->port.is_open()) {
            impl_->port.close(inner);
        }
        impl_->io.stop();
    });

    {
        std::lock_guard lock(impl_->mutex);
        impl_->waiting = false;
        impl_->done = true;
    }
    impl_->cv.notify_all();

    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }

    if (impl_->port.is_open()) {
        impl_->port.close(ec);
    }
    impl_->silenceTimer.cancel();
    impl_->rx.clear();
    impl_->running = false;
    impl_->io.restart();
}

SerialRtuMaster::Result SerialRtuMaster::transact(
    const std::uint8_t* request,
    std::size_t length,
    std::chrono::milliseconds timeout) {
    Result result;
    if (!impl_->running || request == nullptr || length < 4) {
        result.status = Status::Transport;
        result.message = "not connected";
        return result;
    }

    std::vector<std::uint8_t> tx(request, request + length);
    {
        std::lock_guard lock(impl_->mutex);
        impl_->waiting = true;
        impl_->done = false;
        impl_->response.clear();
    }

    boost::asio::post(impl_->strand, [this, tx] {
        impl_->rx.clear();
        if (!impl_->port.is_open()) {
            std::lock_guard lock(impl_->mutex);
            impl_->waiting = false;
            impl_->done = true;
            impl_->cv.notify_all();
            return;
        }
        boost::system::error_code ec;
        boost::asio::write(impl_->port, boost::asio::buffer(tx), ec);
        if (ec) {
            if (impl_->onError && ec != boost::asio::error::operation_aborted) {
                impl_->onError(ec.message());
            }
            std::lock_guard lock(impl_->mutex);
            impl_->waiting = false;
            impl_->done = true;
            impl_->cv.notify_all();
        }
    });

    std::unique_lock lock(impl_->mutex);
    const bool signaled = impl_->cv.wait_for(lock, timeout, [this] { return impl_->done; });
    impl_->waiting = false;
    if (!signaled) {
        result.status = Status::Timeout;
        result.message = "timeout";
        return result;
    }
    if (impl_->response.empty()) {
        result.status = Status::Timeout;
        result.message = "empty response";
        return result;
    }
    if (!emulator::FrameCrcIsValid(impl_->response.data(), impl_->response.size())) {
        result.status = Status::BadCrc;
        result.message = "bad CRC";
        result.frame = impl_->response;
        return result;
    }
    result.status = Status::Ok;
    result.frame = impl_->response;
    return result;
}

void SerialRtuMaster::Impl::doRead() {
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
            if (rx.size() > emulator::kMaxAduSize) {
                rx.erase(rx.begin(), rx.begin() + static_cast<std::ptrdiff_t>(rx.size() - emulator::kMaxAduSize));
            }
            armSilenceTimer();
            doRead();
        }));
}

void SerialRtuMaster::Impl::armSilenceTimer() {
    silenceTimer.expires_after(t35);
    silenceTimer.async_wait(boost::asio::bind_executor(strand, [this](const boost::system::error_code& ec) {
        if (ec) {
            return;
        }
        onSilence();
    }));
}

void SerialRtuMaster::Impl::onSilence() {
    if (rx.size() < 4) {
        return;
    }
    std::vector<std::uint8_t> frame = std::move(rx);
    rx.clear();

    std::lock_guard lock(mutex);
    if (!waiting) {
        return;
    }
    response = std::move(frame);
    waiting = false;
    done = true;
    cv.notify_all();
}

}  // namespace poller
