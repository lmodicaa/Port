#include "inst_loader.h"
#include <cstring>
#include <android/log.h>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ManhuntInst", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ManhuntInst", __VA_ARGS__)

std::vector<EntityInst> parse_inst(const std::vector<uint8_t>& data) {
    std::vector<EntityInst> insts;
    if (data.size() < sizeof(uint32_t)) {
        LOGE("INST: cabecera truncada (%zu bytes)", data.size());
        return insts;
    }

    uint32_t count = 0;
    memcpy(&count, data.data(), sizeof(count));

    // Divide instead of multiplying count*4 to avoid integer overflow.
    if (count > (data.size() - sizeof(uint32_t)) / sizeof(uint32_t)) {
        LOGE("INST: directorio de tamaños truncado (count=%u, bytes=%zu)",
             count, data.size());
        return insts;
    }
    
    std::vector<uint32_t> sizes(count);
    memcpy(sizes.data(), data.data() + 4, count * 4);
    
    size_t offset = sizeof(uint32_t) + static_cast<size_t>(count) * sizeof(uint32_t);

    for (uint32_t i = 0; i < count; i++) {
        const size_t size = sizes[i];
        if (offset > data.size() || size > data.size() - offset) {
            LOGE("INST: registro %u truncado (offset=%zu, size=%zu, total=%zu)",
                 i, offset, size, data.size());
            break;
        }
        
        const uint8_t* block = data.data() + offset;
        EntityInst ent;
        
        size_t s1_len = 0;
        while (s1_len < size && block[s1_len] != '\0') s1_len++;
        ent.name = std::string((const char*)block, s1_len);
        
        size_t pos1 = (s1_len + 1 + 3) & ~3;
        if (pos1 < size) {
            size_t s2_len = 0;
            while (pos1 + s2_len < size && block[pos1 + s2_len] != '\0') s2_len++;
            ent.model = std::string((const char*)(block + pos1), s2_len);
            
            size_t pos2 = (pos1 + s2_len + 1 + 3) & ~3;
            if (pos2 + 28 <= size) {
                memcpy(ent.pos, block + pos2, 12);
                memcpy(ent.rot, block + pos2 + 12, 16);
                
                size_t pos3 = pos2 + 28;
                size_t s3_len = 0;
                while (pos3 + s3_len < size && block[pos3 + s3_len] != '\0') s3_len++;
                ent.entity_class = std::string((const char*)(block + pos3), s3_len);
            }
        }
        insts.push_back(ent);
        offset += size;
    }
    LOGI("Parsed %zu instances", insts.size());
    return insts;
}
