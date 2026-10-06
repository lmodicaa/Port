#include "inst_loader.h"
#include <cstring>
#include <android/log.h>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ManhuntInst", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ManhuntInst", __VA_ARGS__)

std::vector<EntityInst> parse_inst(const std::vector<uint8_t>& data) {
    std::vector<EntityInst> insts;
    if (data.size() < 4) return insts;
    
    uint32_t count = 0;
    memcpy(&count, data.data(), 4);
    
    if (data.size() < 4 + count * 4) return insts;
    
    std::vector<uint32_t> sizes(count);
    memcpy(sizes.data(), data.data() + 4, count * 4);
    
    uint32_t offset = 4 + count * 4;
    
    for (uint32_t i = 0; i < count; i++) {
        uint32_t size = sizes[i];
        if (offset + size > data.size()) break;
        
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
