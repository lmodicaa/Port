#include "level_runtime.h"

#include <android/log.h>
#include <fstream>

#define LEVEL_LOG_TAG "Manhunt"
#define LEVEL_LOGI(...) __android_log_print(ANDROID_LOG_INFO, LEVEL_LOG_TAG, __VA_ARGS__)
#define LEVEL_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LEVEL_LOG_TAG, __VA_ARGS__)

std::vector<uint8_t> level_read_asset(
    AAssetManager* assets,
    const std::string& base_path,
    const char* path
) {
    if (!path) {
        LEVEL_LOGE("Asset path nulo");
        return {};
    }

    if (!base_path.empty()) {
        std::string full_path = base_path + "/" + path;
        std::ifstream file(full_path, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            const std::streampos end = file.tellg();
            if (end >= 0) {
                const size_t size = static_cast<size_t>(end);
                file.seekg(0, std::ios::beg);
                std::vector<uint8_t> buffer(size);
                if (file.read(reinterpret_cast<char*>(buffer.data()),
                              static_cast<std::streamsize>(size))) {
                    LEVEL_LOGI("Archivo cargado externamente: %s (%zu bytes)",
                               full_path.c_str(), size);
                    return buffer;
                }
            }
        }
    }

    if (!assets) return {};
    AAsset* asset = AAssetManager_open(assets, path, AASSET_MODE_BUFFER);
    if (!asset) {
        LEVEL_LOGE("Asset no encontrado: %s", path);
        return {};
    }

    const off_t asset_length = AAsset_getLength(asset);
    if (asset_length < 0) {
        LEVEL_LOGE("Longitud de asset inválida: %s", path);
        AAsset_close(asset);
        return {};
    }
    const size_t size = static_cast<size_t>(asset_length);
    std::vector<uint8_t> buffer(size);
    const int bytes_read = AAsset_read(asset, buffer.data(), size);
    AAsset_close(asset);
    if (bytes_read < 0 || static_cast<size_t>(bytes_read) != size) {
        LEVEL_LOGE("Asset truncado: %s (%d/%zu bytes)", path, bytes_read, size);
        return {};
    }
    return buffer;
}
