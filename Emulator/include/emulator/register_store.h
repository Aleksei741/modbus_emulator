#pragma once

#include "emulator/modbus_types.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace emulator {

class RegisterStore {
public:
    RegisterStore();

    std::uint64_t add(TableKind kind, RegisterCell cell);
    std::size_t addRange(TableKind kind, std::uint8_t device, std::uint16_t start, std::size_t count);
    bool remove(TableKind kind, std::uint64_t id);
    std::size_t removeIds(TableKind kind, const std::vector<std::uint64_t>& ids);
    void clear(TableKind kind);
    void replaceTable(TableKind kind, std::vector<RegisterCell> cells);

    std::size_t size(TableKind kind) const;
    RegisterCell at(TableKind kind, std::size_t row) const;
    std::optional<RegisterCell> findById(TableKind kind, std::uint64_t id) const;
    std::vector<RegisterCell> snapshot(TableKind kind) const;

    bool update(TableKind kind, const RegisterCell& cell);
    bool setRow(TableKind kind, std::size_t row, const RegisterCell& cell);

    bool deviceExists(std::uint8_t device) const;

    std::optional<bool> readBit(TableKind kind, std::uint8_t device, std::uint16_t address) const;
    std::optional<std::uint16_t> readWord(TableKind kind, std::uint8_t device, std::uint16_t address) const;

    bool writeBit(TableKind kind, std::uint8_t device, std::uint16_t address, bool value);
    bool writeWord(TableKind kind, std::uint8_t device, std::uint16_t address, std::uint16_t value);
    bool applyPolledValue(TableKind kind, std::uint8_t device, std::uint16_t address, std::uint16_t value);

    using UserEditCallback = std::function<void(TableKind, std::uint8_t device, std::uint16_t address, std::uint16_t value)>;
    void setOnUserEdit(UserEditCallback callback);

    std::size_t writeBitBroadcast(TableKind kind, std::uint16_t address, bool value);
    std::size_t writeWordBroadcast(TableKind kind, std::uint16_t address, std::uint16_t value);

    bool rangePresent(TableKind kind, std::uint8_t device, std::uint16_t start, std::uint16_t quantity) const;
    std::vector<std::uint16_t> missingAddresses(
        TableKind kind,
        std::uint8_t device,
        std::uint16_t start,
        std::uint16_t quantity) const;

private:
    struct TableData {
        std::vector<RegisterCell> rows;
        std::unordered_map<std::uint32_t, std::size_t> enabledIndex;
    };

    static std::uint32_t makeKey(std::uint8_t device, std::uint16_t address);
    TableData& table(TableKind kind);
    const TableData& table(TableKind kind) const;
    void rebuildLocked(TableKind kind);
    void rebuildDevicesLocked();
    std::uint64_t nextIdLocked();
    bool clampCell(TableKind kind, RegisterCell& cell) const;

    mutable std::mutex mutex_;
    TableData coils_;
    TableData discrete_;
    TableData inputs_;
    TableData holdings_;
    std::unordered_set<std::uint8_t> devices_;
    std::uint64_t nextId_ = 1;
    UserEditCallback onUserEdit_;
};

}  // namespace emulator
