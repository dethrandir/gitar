#pragma once

#include <string>
#include <vector>

namespace gitar {

struct DeviceInfo {
    std::string name;
    bool is_input;
    bool is_output;
    bool is_default;
};

// Enumerates audio devices through miniaudio. Returns an empty vector when no
// backend is available; never throws.
std::vector<DeviceInfo> list_devices();

}  // namespace gitar
