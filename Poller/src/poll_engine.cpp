#include "poller/poll_engine.h"

#include "poller/modbus_master.h"

#include "emulator/modbus_log.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>

namespace poller {

PollEngine::PollEngine(emulator::RegisterStore& store, SerialRtuMaster& master)
    : store_(store), master_(master) {}

PollEngine::~PollEngine() {
    stop();
}

void PollEngine::setInterval(std::chrono::milliseconds interval) {
    const auto ms = static_cast<int>(interval.count());
    intervalMs_ = ms < 50 ? 50 : ms;
}

void PollEngine::setTimeout(std::chrono::milliseconds timeout) {
    const auto ms = static_cast<int>(timeout.count());
    timeoutMs_ = ms < 50 ? 50 : ms;
}

void PollEngine::setCallbacks(LogCallback onLog, ErrorCallback onError) {
    onLog_ = std::move(onLog);
    onError_ = std::move(onError);
}

void PollEngine::queueWrite(
    emulator::TableKind kind,
    std::uint8_t device,
    std::uint16_t address,
    std::uint16_t value) {
    if (!emulator::IsWritableTable(kind)) {
        return;
    }
    std::lock_guard lock(writeMutex_);
    writes_.push_back(WriteJob{kind, device, address, value});
}

bool PollEngine::running() const {
    return running_;
}

bool PollEngine::start() {
    stop();
    if (!master_.running()) {
        return false;
    }
    running_ = true;
    thread_ = std::thread([this] { loop(); });
    return true;
}

void PollEngine::requestStop() {
    running_ = false;
}

void PollEngine::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void PollEngine::logLine(std::string line, bool changed, emulator::TableKind table) {
    if (onLog_) {
        onLog_(std::move(line), changed, table);
    }
}

void PollEngine::loop() {
    while (running_) {
        const auto started = std::chrono::steady_clock::now();
        drainWrites();
        if (running_) {
            pollReads();
        }
        const auto elapsed = std::chrono::steady_clock::now() - started;
        auto sleepFor = std::chrono::milliseconds(intervalMs_.load()) - elapsed;
        if (sleepFor < std::chrono::milliseconds(1)) {
            sleepFor = std::chrono::milliseconds(1);
        }
        const auto slice = std::chrono::milliseconds(20);
        while (running_ && sleepFor > std::chrono::milliseconds(0)) {
            drainWrites();
            const auto step = sleepFor < slice ? sleepFor : slice;
            std::this_thread::sleep_for(step);
            sleepFor -= step;
        }
    }
}

void PollEngine::drainWrites() {
    std::vector<WriteJob> jobs;
    {
        std::lock_guard lock(writeMutex_);
        jobs.swap(writes_);
    }
    for (const auto& job : jobs) {
        if (!running_) {
            return;
        }
        executeWrite(job);
    }
}

std::vector<PollEngine::ReadJob> PollEngine::makeJobs(emulator::TableKind kind) const {
    auto cells = store_.snapshot(kind);
    cells.erase(std::remove_if(cells.begin(), cells.end(), [](const emulator::RegisterCell& cell) {
        return !cell.enabled;
    }), cells.end());
    std::sort(cells.begin(), cells.end(), [](const emulator::RegisterCell& a, const emulator::RegisterCell& b) {
        if (a.device != b.device) {
            return a.device < b.device;
        }
        return a.address < b.address;
    });
    cells.erase(std::unique(cells.begin(), cells.end(), [](const emulator::RegisterCell& a, const emulator::RegisterCell& b) {
        return a.device == b.device && a.address == b.address;
    }), cells.end());

    std::vector<ReadJob> jobs;
    const auto maxQty = MaxQuantity(kind);
    for (const auto& cell : cells) {
        if (!jobs.empty()) {
            auto& last = jobs.back();
            const std::uint32_t next = static_cast<std::uint32_t>(last.start) + last.quantity;
            if (last.device == cell.device && next == cell.address && last.quantity < maxQty) {
                ++last.quantity;
                continue;
            }
        }
        jobs.push_back(ReadJob{kind, cell.device, cell.address, 1});
    }
    return jobs;
}

void PollEngine::pollReads() {
    for (const auto kind : emulator::kAllTables) {
        if (!running_) {
            return;
        }
        const auto jobs = makeJobs(kind);
        for (const auto& job : jobs) {
            if (!running_) {
                return;
            }
            executeRead(job);
            drainWrites();
        }
    }
}

void PollEngine::executeRead(const ReadJob& job) {
    const auto function = ReadFunction(job.kind);
    const auto request = BuildReadRequest(job.device, function, job.start, job.quantity);
    auto result = master_.transact(request.data(), request.size(), std::chrono::milliseconds(timeoutMs_.load()));

    std::string line = std::string(emulator::FunctionTitleRu(function))
        + ": устройство " + std::to_string(job.device)
        + ", адреса " + emulator::AddressSpan(job.start, job.quantity);
    if (job.quantity > 1) {
        line += " (" + std::to_string(job.quantity) + " шт.)";
    }
    line += " | TX [" + HexDump(request.data(), request.size()) + "]";

    if (result.status != SerialRtuMaster::Status::Ok) {
        if (result.status == SerialRtuMaster::Status::Timeout) {
            line += " — нет ответа (таймаут)";
        } else if (result.status == SerialRtuMaster::Status::BadCrc) {
            line += " — ответ с неверной CRC";
        } else {
            line += " — ошибка порта: " + result.message;
        }
        if (!result.frame.empty()) {
            line += " | RX [" + HexDump(result.frame.data(), result.frame.size()) + "]";
        }
        logLine(std::move(line), false, job.kind);
        return;
    }

    line += " | RX [" + HexDump(result.frame.data(), result.frame.size()) + "]";
    auto parsed = ParseResponse(result.frame.data(), result.frame.size(), job.device, function);
    if (parsed.exception) {
        line += " — устройство ответило исключением "
            + std::to_string(parsed.exceptionCode)
            + " (" + emulator::ExceptionTitleRu(parsed.exceptionCode) + ")";
        logLine(std::move(line), false, job.kind);
        return;
    }
    if (!parsed.ok) {
        line += " — непонятный ответ: " + parsed.message;
        logLine(std::move(line), false, job.kind);
        return;
    }

    bool changed = false;
    if (emulator::IsBitTable(job.kind)) {
        std::vector<bool> bits;
        if (!UnpackBits(parsed.payload, job.quantity, bits)) {
            line += " — в ответе неверные биты";
            logLine(std::move(line), false, job.kind);
            return;
        }
        for (std::uint16_t i = 0; i < job.quantity; ++i) {
            if (store_.applyPolledValue(job.kind, job.device, static_cast<std::uint16_t>(job.start + i), bits[i] ? 1 : 0)) {
                changed = true;
            }
        }
    } else {
        std::vector<std::uint16_t> words;
        if (!UnpackWords(parsed.payload, job.quantity, words)) {
            line += " — в ответе неверные регистры";
            logLine(std::move(line), false, job.kind);
            return;
        }
        for (std::uint16_t i = 0; i < job.quantity; ++i) {
            if (store_.applyPolledValue(job.kind, job.device, static_cast<std::uint16_t>(job.start + i), words[i])) {
                changed = true;
            }
        }
    }
    line += " → ок";
    logLine(std::move(line), changed, job.kind);
}

void PollEngine::executeWrite(const WriteJob& job) {
    std::vector<std::uint8_t> request;
    std::uint8_t function = 0;
    if (job.kind == emulator::TableKind::Coils) {
        function = emulator::kFcWriteSingleCoil;
        request = BuildWriteCoilRequest(job.device, job.address, job.value != 0);
    } else {
        function = emulator::kFcWriteSingleRegister;
        request = BuildWriteRegisterRequest(job.device, job.address, job.value);
    }

    auto result = master_.transact(request.data(), request.size(), std::chrono::milliseconds(timeoutMs_.load()));
    std::string line = std::string(emulator::FunctionTitleRu(function))
        + ": устройство " + std::to_string(job.device)
        + ", адрес " + std::to_string(job.address)
        + ", значение " + std::to_string(job.value)
        + " | TX [" + HexDump(request.data(), request.size()) + "]";

    if (result.status != SerialRtuMaster::Status::Ok) {
        if (result.status == SerialRtuMaster::Status::Timeout) {
            line += " — нет ответа (таймаут)";
        } else if (result.status == SerialRtuMaster::Status::BadCrc) {
            line += " — ответ с неверной CRC";
        } else {
            line += " — ошибка порта: " + result.message;
        }
        if (!result.frame.empty()) {
            line += " | RX [" + HexDump(result.frame.data(), result.frame.size()) + "]";
        }
        logLine(std::move(line), false, job.kind);
        return;
    }
    line += " | RX [" + HexDump(result.frame.data(), result.frame.size()) + "]";
    auto parsed = ParseResponse(result.frame.data(), result.frame.size(), job.device, function);
    if (parsed.exception) {
        line += " — устройство ответило исключением "
            + std::to_string(parsed.exceptionCode)
            + " (" + emulator::ExceptionTitleRu(parsed.exceptionCode) + ")";
    } else if (!parsed.ok) {
        line += " — непонятный ответ: " + parsed.message;
    } else {
        line += " → ок";
    }
    logLine(std::move(line), false, job.kind);
}

}  // namespace poller
