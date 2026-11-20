#include "emulator/register_store.h"

#include <algorithm>

namespace emulator {

namespace {

bool isBit(TableKind kind) {
    return IsBitTable(kind);
}

}  // namespace

RegisterStore::RegisterStore() = default;

std::uint32_t RegisterStore::makeKey(std::uint8_t device, std::uint16_t address) {
    return (static_cast<std::uint32_t>(device) << 16) | static_cast<std::uint32_t>(address);
}

RegisterStore::TableData& RegisterStore::table(TableKind kind) {
    switch (kind) {
    case TableKind::Coils:
        return coils_;
    case TableKind::DiscreteInputs:
        return discrete_;
    case TableKind::InputRegisters:
        return inputs_;
    case TableKind::HoldingRegisters:
        return holdings_;
    }
    return holdings_;
}

const RegisterStore::TableData& RegisterStore::table(TableKind kind) const {
    switch (kind) {
    case TableKind::Coils:
        return coils_;
    case TableKind::DiscreteInputs:
        return discrete_;
    case TableKind::InputRegisters:
        return inputs_;
    case TableKind::HoldingRegisters:
        return holdings_;
    }
    return holdings_;
}

bool RegisterStore::clampCell(TableKind kind, RegisterCell& cell) const {
    if (cell.device < kMinDeviceAddress || cell.device > kMaxDeviceAddress) {
        return false;
    }
    if (isBit(kind) && cell.value > 1) {
        cell.value = 1;
    }
    return true;
}

std::uint64_t RegisterStore::nextIdLocked() {
    return nextId_++;
}

void RegisterStore::rebuildLocked(TableKind kind) {
    auto& data = table(kind);
    data.enabledIndex.clear();
    for (std::size_t i = 0; i < data.rows.size(); ++i) {
        const auto& cell = data.rows[i];
        if (!cell.enabled) {
            continue;
        }
        data.enabledIndex[makeKey(cell.device, cell.address)] = i;
    }
}

void RegisterStore::rebuildDevicesLocked() {
    devices_.clear();
    const TableData* tables[] = {&coils_, &discrete_, &inputs_, &holdings_};
    for (const auto* data : tables) {
        for (const auto& cell : data->rows) {
            if (cell.enabled) {
                devices_.insert(cell.device);
            }
        }
    }
}

std::uint64_t RegisterStore::add(TableKind kind, RegisterCell cell) {
    std::lock_guard lock(mutex_);
    auto& data = table(kind);
    if (data.rows.size() >= kMaxRowsPerTable) {
        return 0;
    }
    if (!clampCell(kind, cell)) {
        return 0;
    }
    cell.id = nextIdLocked();
    data.rows.push_back(cell);
    rebuildLocked(kind);
    rebuildDevicesLocked();
    return cell.id;
}

std::size_t RegisterStore::addRange(TableKind kind, std::uint8_t device, std::uint16_t start, std::size_t count) {
    if (count == 0 || device < kMinDeviceAddress || device > kMaxDeviceAddress) {
        return 0;
    }
    std::lock_guard lock(mutex_);
    auto& data = table(kind);
    std::size_t added = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (data.rows.size() >= kMaxRowsPerTable) {
            break;
        }
        const std::uint32_t address = static_cast<std::uint32_t>(start) + static_cast<std::uint32_t>(i);
        if (address > kMaxRegisterAddress) {
            break;
        }
        const auto key = makeKey(device, static_cast<std::uint16_t>(address));
        bool exists = false;
        for (const auto& row : data.rows) {
            if (row.device == device && row.address == static_cast<std::uint16_t>(address)) {
                exists = true;
                break;
            }
        }
        if (exists) {
            continue;
        }
        RegisterCell cell;
        cell.id = nextIdLocked();
        cell.enabled = true;
        cell.device = device;
        cell.address = static_cast<std::uint16_t>(address);
        cell.value = 0;
        data.rows.push_back(cell);
        data.enabledIndex[key] = data.rows.size() - 1;
        ++added;
    }
    if (added > 0) {
        rebuildDevicesLocked();
    }
    return added;
}

bool RegisterStore::remove(TableKind kind, std::uint64_t id) {
    return removeIds(kind, {id}) == 1;
}

std::size_t RegisterStore::removeIds(TableKind kind, const std::vector<std::uint64_t>& ids) {
    if (ids.empty()) {
        return 0;
    }
    std::lock_guard lock(mutex_);
    auto& data = table(kind);
    const auto oldSize = data.rows.size();
    data.rows.erase(
        std::remove_if(data.rows.begin(), data.rows.end(), [&](const RegisterCell& cell) {
            return std::find(ids.begin(), ids.end(), cell.id) != ids.end();
        }),
        data.rows.end());
    const auto removed = oldSize - data.rows.size();
    if (removed > 0) {
        rebuildLocked(kind);
        rebuildDevicesLocked();
    }
    return removed;
}

void RegisterStore::clear(TableKind kind) {
    std::lock_guard lock(mutex_);
    auto& data = table(kind);
    data.rows.clear();
    data.enabledIndex.clear();
    rebuildDevicesLocked();
}

void RegisterStore::replaceTable(TableKind kind, std::vector<RegisterCell> cells) {
    std::lock_guard lock(mutex_);
    auto& data = table(kind);
    data.rows.clear();
    data.rows.reserve(std::min(cells.size(), kMaxRowsPerTable));
    for (auto& cell : cells) {
        if (data.rows.size() >= kMaxRowsPerTable) {
            break;
        }
        if (!clampCell(kind, cell)) {
            continue;
        }
        if (cell.id == 0) {
            cell.id = nextIdLocked();
        } else {
            nextId_ = std::max(nextId_, cell.id + 1);
        }
        data.rows.push_back(cell);
    }
    rebuildLocked(kind);
    rebuildDevicesLocked();
}

std::size_t RegisterStore::size(TableKind kind) const {
    std::lock_guard lock(mutex_);
    return table(kind).rows.size();
}

RegisterCell RegisterStore::at(TableKind kind, std::size_t row) const {
    std::lock_guard lock(mutex_);
    const auto& data = table(kind);
    if (row >= data.rows.size()) {
        return {};
    }
    return data.rows[row];
}

std::optional<RegisterCell> RegisterStore::findById(TableKind kind, std::uint64_t id) const {
    std::lock_guard lock(mutex_);
    const auto& data = table(kind);
    for (const auto& cell : data.rows) {
        if (cell.id == id) {
            return cell;
        }
    }
    return std::nullopt;
}

std::vector<RegisterCell> RegisterStore::snapshot(TableKind kind) const {
    std::lock_guard lock(mutex_);
    return table(kind).rows;
}

void RegisterStore::setOnUserEdit(UserEditCallback callback) {
    std::lock_guard lock(mutex_);
    onUserEdit_ = std::move(callback);
}

bool RegisterStore::update(TableKind kind, const RegisterCell& cell) {
    UserEditCallback callback;
    RegisterCell edited;
    bool fire = false;
    {
        std::lock_guard lock(mutex_);
        auto& data = table(kind);
        for (auto& row : data.rows) {
            if (row.id == cell.id) {
                auto next = cell;
                if (!clampCell(kind, next)) {
                    return false;
                }
                next.id = row.id;
                fire = row.value != next.value;
                edited = next;
                callback = onUserEdit_;
                row = next;
                rebuildLocked(kind);
                rebuildDevicesLocked();
                break;
            }
        }
    }
    if (fire && callback) {
        callback(kind, edited.device, edited.address, edited.value);
    }
    return fire || edited.id != 0;
}

bool RegisterStore::setRow(TableKind kind, std::size_t row, const RegisterCell& cell) {
    UserEditCallback callback;
    RegisterCell edited;
    bool fire = false;
    {
        std::lock_guard lock(mutex_);
        auto& data = table(kind);
        if (row >= data.rows.size()) {
            return false;
        }
        auto next = cell;
        if (!clampCell(kind, next)) {
            return false;
        }
        next.id = data.rows[row].id;
        fire = data.rows[row].value != next.value;
        edited = next;
        callback = onUserEdit_;
        data.rows[row] = next;
        rebuildLocked(kind);
        rebuildDevicesLocked();
    }
    if (fire && callback) {
        callback(kind, edited.device, edited.address, edited.value);
    }
    return true;
}

bool RegisterStore::deviceExists(std::uint8_t device) const {
    std::lock_guard lock(mutex_);
    return devices_.contains(device);
}

std::optional<bool> RegisterStore::readBit(TableKind kind, std::uint8_t device, std::uint16_t address) const {
    std::lock_guard lock(mutex_);
    const auto& data = table(kind);
    const auto it = data.enabledIndex.find(makeKey(device, address));
    if (it == data.enabledIndex.end()) {
        return std::nullopt;
    }
    return data.rows[it->second].value != 0;
}

std::optional<std::uint16_t> RegisterStore::readWord(TableKind kind, std::uint8_t device, std::uint16_t address) const {
    std::lock_guard lock(mutex_);
    const auto& data = table(kind);
    const auto it = data.enabledIndex.find(makeKey(device, address));
    if (it == data.enabledIndex.end()) {
        return std::nullopt;
    }
    return data.rows[it->second].value;
}

bool RegisterStore::writeBit(TableKind kind, std::uint8_t device, std::uint16_t address, bool value) {
    std::lock_guard lock(mutex_);
    auto& data = table(kind);
    const auto it = data.enabledIndex.find(makeKey(device, address));
    if (it == data.enabledIndex.end()) {
        return false;
    }
    data.rows[it->second].value = value ? 1 : 0;
    return true;
}

bool RegisterStore::writeWord(TableKind kind, std::uint8_t device, std::uint16_t address, std::uint16_t value) {
    std::lock_guard lock(mutex_);
    auto& data = table(kind);
    const auto it = data.enabledIndex.find(makeKey(device, address));
    if (it == data.enabledIndex.end()) {
        return false;
    }
    data.rows[it->second].value = value;
    return true;
}

bool RegisterStore::applyPolledValue(TableKind kind, std::uint8_t device, std::uint16_t address, std::uint16_t value) {
    std::lock_guard lock(mutex_);
    auto& data = table(kind);
    bool any = false;
    for (auto& row : data.rows) {
        if (row.enabled && row.device == device && row.address == address) {
            row.value = IsBitTable(kind) ? (value != 0 ? 1 : 0) : value;
            any = true;
        }
    }
    return any;
}

std::size_t RegisterStore::writeBitBroadcast(TableKind kind, std::uint16_t address, bool value) {
    std::lock_guard lock(mutex_);
    auto& data = table(kind);
    std::size_t written = 0;
    for (auto& cell : data.rows) {
        if (cell.enabled && cell.address == address) {
            cell.value = value ? 1 : 0;
            ++written;
        }
    }
    return written;
}

std::size_t RegisterStore::writeWordBroadcast(TableKind kind, std::uint16_t address, std::uint16_t value) {
    std::lock_guard lock(mutex_);
    auto& data = table(kind);
    std::size_t written = 0;
    for (auto& cell : data.rows) {
        if (cell.enabled && cell.address == address) {
            cell.value = value;
            ++written;
        }
    }
    return written;
}

bool RegisterStore::rangePresent(TableKind kind, std::uint8_t device, std::uint16_t start, std::uint16_t quantity) const {
    if (quantity == 0) {
        return false;
    }
    std::lock_guard lock(mutex_);
    const auto& data = table(kind);
    for (std::uint32_t i = 0; i < quantity; ++i) {
        const std::uint32_t address = static_cast<std::uint32_t>(start) + i;
        if (address > kMaxRegisterAddress) {
            return false;
        }
        if (!data.enabledIndex.contains(makeKey(device, static_cast<std::uint16_t>(address)))) {
            return false;
        }
    }
    return true;
}

std::vector<std::uint16_t> RegisterStore::missingAddresses(
    TableKind kind,
    std::uint8_t device,
    std::uint16_t start,
    std::uint16_t quantity) const {
    std::vector<std::uint16_t> missing;
    if (quantity == 0) {
        return missing;
    }
    std::lock_guard lock(mutex_);
    const auto& data = table(kind);
    missing.reserve(quantity);
    for (std::uint32_t i = 0; i < quantity; ++i) {
        const std::uint32_t address = static_cast<std::uint32_t>(start) + i;
        if (address > kMaxRegisterAddress) {
            break;
        }
        if (!data.enabledIndex.contains(makeKey(device, static_cast<std::uint16_t>(address)))) {
            missing.push_back(static_cast<std::uint16_t>(address));
        }
    }
    return missing;
}

}  // namespace emulator
