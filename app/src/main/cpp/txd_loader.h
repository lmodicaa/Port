#pragma once
#include <cstdint>
#include <vector>
#include <string>

// Resultado de cargar una textura desde un TXD
struct TXDTexture {
    std::string name;
    int width  = 0;
    int height = 0;
    std::vector<uint8_t> rgba;  // siempre RGBA8 sin importar el formato origen
    bool valid = false;
};

// Carga todas las texturas de un bloque de memoria TXD (datos crudos del archivo)
std::vector<TXDTexture> txd_load_all(const uint8_t* data, size_t size);
