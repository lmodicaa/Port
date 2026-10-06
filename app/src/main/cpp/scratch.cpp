
std::map<std::string, DFFModel> dff_load_archive(const uint8_t* data, size_t size) {
    std::map<std::string, DFFModel> archive;
    Reader r{data, size};
    
    while (r.pos + sizeof(ChunkHeader) <= r.total) {
        ChunkHeader hdr = r.read_chunk();
        if (hdr.type == RW_CLUMP) {
            size_t clump_end = r.pos + hdr.size;
            
            // 1. Encontrar el Node Name dentro de este Clump
            std::string clump_name = "unknown";
            size_t search_pos = r.pos;
            while (search_pos + sizeof(ChunkHeader) <= clump_end) {
                ChunkHeader sh;
                memcpy(&sh, r.base + search_pos, sizeof(ChunkHeader));
                if (sh.type == 0x0253F2FE) { // Node Name Plugin
                    clump_name = std::string((const char*)(r.base + search_pos + sizeof(ChunkHeader)), sh.size);
                    while(!clump_name.empty() && clump_name.back() == '\0') clump_name.pop_back();
                    break;
                }
                search_pos += sizeof(ChunkHeader) + sh.size;
            }
            
            // 2. Extraer el Geometry de este Clump
            // Reutilizamos dff_load pasándole solo los datos de este Clump
            DFFModel model = dff_load(r.base + r.pos, hdr.size);
            if (model.valid && clump_name != "unknown") {
                archive[clump_name] = model;
                LOGI("Cargado clump: %s", clump_name.c_str());
            }
            
            r.pos = clump_end;
        } else {
            r.skip(hdr.size);
        }
    }
    
    return archive;
}
