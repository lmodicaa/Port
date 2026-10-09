#pragma once

#include <android/asset_manager.h>
#include <cstdint>
#include <string>
#include <vector>

std::vector<uint8_t> level_read_asset(
    AAssetManager* assets,
    const std::string& base_path,
    const char* path
);
