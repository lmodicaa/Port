#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <map>

uint32_t read_u32(const uint8_t* data) {
    uint32_t val;
    std::memcpy(&val, data, 4);
    return val;
}

int main() {
    std::ifstream file("C:\\Users\\Administrator\\Documents\\Port\\app\\src\\main\\assets\\levels\\asylum\\allanims.ifp", std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open file" << std::endl;
        return 1;
    }

    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);

    size_t pos = 0;
    while (pos + 8 <= size) {
        std::string type(reinterpret_cast<const char*>(data.data() + pos), 4);
        uint32_t sz = read_u32(data.data() + pos + 4);
        pos += 8;

        if (type == "BLOC") {
            pos += 24; // Skip name? Wait, standard IFP BLOC has 24 byte name? No, just size. But wait! If we just skip to ANPK?
            // Actually, BLOC is just a container! Let's just find "ANPK" in the file!
        }
    }

    // Better: scan for ANPK
    pos = 0;
    std::map<uint32_t, int> track_counts;
    while (pos < size - 4) {
        if (std::memcmp(data.data() + pos, "ANPK", 4) == 0) {
            uint32_t anpk_size = read_u32(data.data() + pos + 4);
            size_t start = pos + 8;
            
            std::string subtype(reinterpret_cast<const char*>(data.data() + start), 4);
            uint32_t subsz = read_u32(data.data() + start + 4);
            start += 8;
            
            std::string name;
            if (subtype == "NAME") {
                name = std::string(reinterpret_cast<const char*>(data.data() + start), subsz);
                while(!name.empty() && name.back() == '\0') name.pop_back();
                start += subsz;
            }
            
            uint32_t num_bones = read_u32(data.data() + start);
            track_counts[num_bones]++;
            if (num_bones >= 30) {
                std::cout << "Anim: " << name << " tracks: " << num_bones << std::endl;
            }
            pos += anpk_size + 8; // skip entire ANPK
        } else {
            pos++;
        }
    }
    
    std::cout << "Track counts summary:" << std::endl;
    for (auto kv : track_counts) {
        std::cout << kv.first << " tracks: " << kv.second << " anims" << std::endl;
    }

    return 0;
}
