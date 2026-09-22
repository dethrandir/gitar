#include "gitar/devices.hpp"

#include <cstring>

#include "miniaudio.h"

namespace gitar {
namespace {

bool same_name(const char* lhs, const char* rhs) {
    return lhs != nullptr && rhs != nullptr && std::strcmp(lhs, rhs) == 0;
}

}  // namespace

std::vector<DeviceInfo> list_devices() {
    std::vector<DeviceInfo> devices;

    ma_context context;
    if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
        return devices;
    }

    try {
        ma_device_info* playback = nullptr;
        ma_uint32 playback_count = 0;
        ma_device_info* capture = nullptr;
        ma_uint32 capture_count = 0;
        if (ma_context_get_devices(&context, &playback, &playback_count, &capture,
                                   &capture_count) == MA_SUCCESS) {
            const char* default_playback = nullptr;
            const char* default_capture = nullptr;
            for (ma_uint32 i = 0; i < playback_count; ++i) {
                if (playback[i].isDefault) {
                    default_playback = playback[i].name;
                }
            }
            for (ma_uint32 i = 0; i < capture_count; ++i) {
                if (capture[i].isDefault) {
                    default_capture = capture[i].name;
                }
            }
            for (ma_uint32 i = 0; i < playback_count; ++i) {
                devices.push_back(DeviceInfo{playback[i].name, false, true,
                                             same_name(playback[i].name, default_playback)});
            }
            for (ma_uint32 i = 0; i < capture_count; ++i) {
                devices.push_back(DeviceInfo{capture[i].name, true, false,
                                             same_name(capture[i].name, default_capture)});
            }
        }
    } catch (...) {
        devices.clear();
    }

    ma_context_uninit(&context);
    return devices;
}

}  // namespace gitar
