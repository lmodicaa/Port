#include "ifp_loader.h"
#include <android/log.h>
#include <cstring>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "Port-IFP", __VA_ARGS__)

std::map<std::string, Animation> load_ifp(const uint8_t* data, size_t size) {
    std::map<std::string, Animation> anims;
    if (size < 16) return anims;
    
    size_t pos = 0;
    auto read_fourcc = [&](size_t offset) -> std::string {
        if (offset + 4 > size) return "";
        char buf[5] = {0};
        std::memcpy(buf, data + offset, 4);
        return std::string(buf);
    };
    auto read_u32 = [&](size_t offset) -> uint32_t {
        if (offset + 4 > size) return 0;
        uint32_t val;
        std::memcpy(&val, data + offset, 4);
        return val;
    };

    std::string magic = read_fourcc(pos);
    if (magic != "ANCT") return anims;
    pos += 8; // ANCT + 04 00 00 00

    while (pos + 8 <= size) {
        std::string type = read_fourcc(pos);
        uint32_t sz = read_u32(pos + 4);
        pos += 8;
        
        if (type == "BLOC") {
            // BLOC string name
            pos += sz; 
        } else if (type == "ANPK") {
            size_t anpk_end = pos + sz;
            
            std::string subtype = read_fourcc(pos);
            uint32_t subsz = read_u32(pos + 4);
            pos += 8;
            
            Animation current_anim;
            if (subtype == "NAME") {
                std::string name(reinterpret_cast<const char*>(data + pos), subsz);
                while(!name.empty() && name.back() == '\0') name.pop_back();
                current_anim.name = name;
                pos += subsz;
            }
            
            uint32_t num_bones = read_u32(pos);
            uint32_t unk_size = read_u32(pos + 4);
            std::memcpy(&current_anim.duration, data + pos + 8, 4);
            pos += 12;
            
            for (uint32_t i = 0; i < num_bones; i++) {
                if (read_fourcc(pos) == "SEQU") {
                    pos += 4;
                    uint16_t bone_id; std::memcpy(&bone_id, data + pos, 2);
                    uint8_t k_type = data[pos + 2];
                    uint8_t num_frames = data[pos + 3];
                    pos += 4;
                    
                    AnimationTrack track;
                    track.bone_id = bone_id;
                    track.bone_name = "bone_" + std::to_string(bone_id);
                    
                    for (uint8_t f = 0; f < num_frames; f++) {
                        AnimationKeyframe kf = {0};
                        int16_t time_ticks; std::memcpy(&time_ticks, data + pos, 2);
                        kf.time = time_ticks / 60.0f; // Guessing 60 ticks per second? Or just frame index
                        pos += 3; // time (2) + pad (1)
                        
                        int16_t qx, qy, qz, qw;
                        std::memcpy(&qx, data + pos, 2); pos += 2;
                        std::memcpy(&qy, data + pos, 2); pos += 2;
                        std::memcpy(&qz, data + pos, 2); pos += 2;
                        std::memcpy(&qw, data + pos, 2); pos += 2;
                        
                        kf.qx = qx / 4096.0f;
                        kf.qy = qy / 4096.0f;
                        kf.qz = qz / 4096.0f;
                        kf.qw = qw / 4096.0f;
                        
                        if (k_type == 3) {
                            int16_t px, py, pz;
                            std::memcpy(&px, data + pos, 2); pos += 2;
                            std::memcpy(&py, data + pos, 2); pos += 2;
                            std::memcpy(&pz, data + pos, 2); pos += 2;
                            kf.tx = px / 1024.0f;
                            kf.ty = py / 1024.0f;
                            kf.tz = pz / 1024.0f;
                        } else {
                            kf.tx = kf.ty = kf.tz = 0.0f;
                        }
                        track.keyframes.push_back(kf);
                    }
                    current_anim.tracks.push_back(track);
                }
            }
            
            if (!current_anim.name.empty()) {
                anims[current_anim.name] = current_anim;
                LOGI("Cargada Anim: %s con %d huesos", current_anim.name.c_str(), num_bones);
            }
            pos = anpk_end;
        } else {
            pos += sz;
        }
    }
    return anims;
}
