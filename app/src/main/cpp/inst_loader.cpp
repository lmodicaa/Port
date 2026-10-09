#include "inst_loader.h"
#include <cstring>
#include <android/log.h>
#include <algorithm>
#include <utility>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ManhuntInst", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ManhuntInst", __VA_ARGS__)

std::vector<EntityInst> parse_inst(const std::vector<uint8_t>& data) {
    std::vector<EntityInst> insts;
    if (data.size() < sizeof(uint32_t)) {
        LOGE("INST: cabecera truncada (%zu bytes)", data.size());
        return insts;
    }

    uint32_t count = 0;
    std::memcpy(&count, data.data(), sizeof(count));

    // Avoid multiplication overflow when validating the size directory.
    if (count > (data.size() - sizeof(uint32_t)) / sizeof(uint32_t)) {
        LOGE("INST: directorio de tamaños truncado (count=%u, bytes=%zu)",
             count, data.size());
        return insts;
    }

    std::vector<uint32_t> sizes(count);
    const size_t directory_bytes = static_cast<size_t>(count) * sizeof(uint32_t);
    if (directory_bytes != 0) {
        std::memcpy(sizes.data(), data.data() + sizeof(uint32_t), directory_bytes);
    }

    size_t offset = sizeof(uint32_t) + directory_bytes;
    for (uint32_t i = 0; i < count; ++i) {
        const size_t record_size = static_cast<size_t>(sizes[i]);
        if (offset > data.size() || record_size > data.size() - offset) {
            LOGE("INST: registro %u truncado (offset=%zu, size=%zu, total=%zu)",
                 i, offset, record_size, data.size());
            break;
        }

        const size_t record_end = offset + record_size;
        const uint8_t* block = data.data() + offset;
        bool valid = record_size != 0;
        EntityInst ent{};

        auto find_nul = [block, record_size](size_t start, size_t& end) -> bool {
            if (start >= record_size) return false;
            for (size_t p = start; p < record_size; ++p) {
                if (block[p] == '\0') {
                    end = p;
                    return true;
                }
            }
            return false;
        };

        size_t name_end = 0;
        size_t model_end = 0;
        size_t pos1 = 0;
        size_t pos2 = 0;

        if (valid && !find_nul(0, name_end)) {
            LOGE("INST: registro %u sin terminador NUL en name", i);
            valid = false;
        }

        if (valid) {
            ent.name.assign(reinterpret_cast<const char*>(block), name_end);
            pos1 = (name_end + 1 + 3) & ~static_cast<size_t>(3);
            if (pos1 >= record_size || !find_nul(pos1, model_end)) {
                LOGE("INST: registro %u sin model completo", i);
                valid = false;
            }
        }

        if (valid) {
            ent.model.assign(reinterpret_cast<const char*>(block + pos1), model_end - pos1);
            pos2 = (model_end + 1 + 3) & ~static_cast<size_t>(3);
            if (pos2 > record_size || record_size - pos2 < 28) {
                LOGE("INST: registro %u sin posición/rotación completas", i);
                valid = false;
            }
        }

        if (valid) {
            std::memcpy(ent.pos, block + pos2, sizeof(ent.pos));
            std::memcpy(ent.rot, block + pos2 + sizeof(ent.pos), sizeof(ent.rot));

            const size_t class_start = pos2 + 28;
            if (class_start < record_size) {
                size_t class_end = class_start;
                if (!find_nul(class_start, class_end)) class_end = record_size;
                ent.entity_class.assign(
                    reinterpret_cast<const char*>(block + class_start),
                    class_end - class_start
                );
            }

            // Publish only after every required field was parsed successfully.
            insts.push_back(std::move(ent));
        }

        offset = record_end;
    }

    LOGI("Parsed %zu instances (declared=%u)", insts.size(), count);
    return insts;
}
