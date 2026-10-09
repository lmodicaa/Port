#include "col_loader.h"

#include <android/log.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

#if defined(MANHUNT_VERBOSE_ASSET_LOGS)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ManhuntCOL", __VA_ARGS__)
#else
#define LOGI(...) do { } while (0)
#endif
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ManhuntCOL", __VA_ARGS__)

namespace {

class Reader {
public:
    Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    bool can_read(size_t count) const {
        return pos_ <= size_ && count <= size_ - pos_;
    }

    size_t position() const {
        return pos_;
    }

    size_t remaining() const {
        return pos_ <= size_ ? size_ - pos_ : 0;
    }

    bool skip(size_t count) {
        if (!can_read(count)) return false;
        pos_ += count;
        return true;
    }

    template <typename T>
    bool read(T& out) {
        if (!can_read(sizeof(T))) return false;
        std::memcpy(&out, data_ + pos_, sizeof(T));
        pos_ += sizeof(T);
        return true;
    }

    bool read_bytes(void* out, size_t count) {
        if (!can_read(count)) return false;
        std::memcpy(out, data_ + pos_, count);
        pos_ += count;
        return true;
    }

    bool read_string_padded4(std::string& out) {
        out.clear();
        const size_t start = pos_;

        while (can_read(1)) {
            uint8_t c = data_[pos_++];
            if (c == 0) {
                const size_t consumed = pos_ - start;
                const size_t pad = (4 - (consumed & 3)) & 3;
                return skip(pad);
            }
            out.push_back(static_cast<char>(c));
        }

        return false;
    }

private:
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
    size_t pos_ = 0;
};

bool read_vec3(Reader& r, ColVec3& v) {
    return r.read(v.x) && r.read(v.y) && r.read(v.z);
}

bool read_surface(Reader& r, ColSurface& s) {
    return r.read(s.material) &&
           r.read(s.flag) &&
           r.read(s.brightness) &&
           r.read(s.light);
}

bool valid_count(int32_t count, int32_t max_count = 2000000) {
    return count >= 0 && count <= max_count;
}

} // namespace

std::vector<ColModel> col_load_all(const uint8_t* data, size_t size) {
    std::vector<ColModel> result;

    if (!data || size < sizeof(int32_t)) {
        LOGE("COL vacío o demasiado pequeño (%zu bytes)", size);
        return result;
    }

    Reader r(data, size);

    int32_t model_count = 0;
    if (!r.read(model_count) || !valid_count(model_count, 10000)) {
        LOGE("COL: cantidad de modelos inválida");
        return result;
    }

    result.reserve(static_cast<size_t>(model_count));

    for (int32_t model_index = 0; model_index < model_count; ++model_index) {
        const size_t model_start = r.position();
        ColModel model;

        if (!r.read_string_padded4(model.name)) {
            LOGE("COL[%d]: nombre truncado", model_index);
            break;
        }

        if (!read_vec3(r, model.center) ||
            !r.read(model.radius) ||
            !read_vec3(r, model.min) ||
            !read_vec3(r, model.max)) {
            LOGE("COL[%d] %s: bounds truncados", model_index, model.name.c_str());
            break;
        }

        int32_t count = 0;

        if (!r.read(count) || !valid_count(count) ||
            static_cast<size_t>(count) > r.remaining() / 20u) {
            LOGE("COL[%d] %s: sphere count inválido o fuera de límites", model_index, model.name.c_str());
            break;
        }
        model.spheres.reserve(static_cast<size_t>(count));
        for (int32_t i = 0; i < count; ++i) {
            ColSphere sphere;
            if (!read_vec3(r, sphere.center) ||
                !r.read(sphere.radius) ||
                !read_surface(r, sphere.surface)) {
                LOGE("COL[%d] %s: sphere truncada", model_index, model.name.c_str());
                return result;
            }
            model.spheres.push_back(sphere);
        }

        if (!r.read(count) || !valid_count(count) ||
            static_cast<size_t>(count) > r.remaining() / 24u) {
            LOGE("COL[%d] %s: line count inválido o fuera de límites", model_index, model.name.c_str());
            break;
        }
        model.lines.reserve(static_cast<size_t>(count));
        for (int32_t i = 0; i < count; ++i) {
            ColLine line;
            if (!read_vec3(r, line.a) || !read_vec3(r, line.b)) {
                LOGE("COL[%d] %s: line truncada", model_index, model.name.c_str());
                return result;
            }
            model.lines.push_back(line);
        }

        if (!r.read(count) || !valid_count(count) ||
            static_cast<size_t>(count) > r.remaining() / 28u) {
            LOGE("COL[%d] %s: box count inválido o fuera de límites", model_index, model.name.c_str());
            break;
        }
        model.boxes.reserve(static_cast<size_t>(count));
        for (int32_t i = 0; i < count; ++i) {
            ColBox box;
            if (!read_vec3(r, box.min) ||
                !read_vec3(r, box.max) ||
                !read_surface(r, box.surface)) {
                LOGE("COL[%d] %s: box truncada", model_index, model.name.c_str());
                return result;
            }
            model.boxes.push_back(box);
        }

        if (!r.read(count) || !valid_count(count) ||
            static_cast<size_t>(count) > r.remaining() / 12u) {
            LOGE("COL[%d] %s: vertex count inválido o fuera de límites", model_index, model.name.c_str());
            break;
        }
        model.vertices.reserve(static_cast<size_t>(count));
        for (int32_t i = 0; i < count; ++i) {
            ColVec3 v;
            if (!read_vec3(r, v)) {
                LOGE("COL[%d] %s: vértice truncado", model_index, model.name.c_str());
                return result;
            }
            model.vertices.push_back(v);
        }

        if (!r.read(count) || !valid_count(count) ||
            static_cast<size_t>(count) > r.remaining() / 6u) {
            LOGE("COL[%d] %s: face count inválido o fuera de límites", model_index, model.name.c_str());
            break;
        }
        model.faces.reserve(static_cast<size_t>(count));
        for (int32_t i = 0; i < count; ++i) {
            ColFace face;
            if (!r.read(face.a) || !r.read(face.b) || !r.read(face.c)) {
                LOGE("COL[%d] %s: face truncada", model_index, model.name.c_str());
                return result;
            }
            model.faces.push_back(face);
        }

        result.push_back(std::move(model));

        const ColModel& loaded = result.back();
        LOGI(
            "COL[%d] %s: spheres=%zu lines=%zu boxes=%zu verts=%zu faces=%zu bytes=%zu",
            model_index,
            loaded.name.c_str(),
            loaded.spheres.size(),
            loaded.lines.size(),
            loaded.boxes.size(),
            loaded.vertices.size(),
            loaded.faces.size(),
            r.position() - model_start
        );
    }

    LOGI("COL cargado: %zu modelos de %d", result.size(), model_count);
    for (size_t i = 0; i < result.size() && i < 40; ++i) {
        const auto& model = result[i];
        LOGI(
            "COL MODEL[%zu] %s bounds=(%.2f,%.2f,%.2f)-(%.2f,%.2f,%.2f)",
            i,
            model.name.c_str(),
            model.min.x, model.min.y, model.min.z,
            model.max.x, model.max.y, model.max.z
        );
    }
    return result;
}
