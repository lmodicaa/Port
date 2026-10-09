#include "ifp_loader.h"

#include <cstring>
#include <stdexcept>
#include <string>
#include <map>
#include <vector>
#include <android/log.h>
#include <cstdio>

#define LOG_TAG "Port-IFP"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#if defined(MANHUNT_VERBOSE_ASSET_LOGS)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#else
#define LOGI(...) do { } while (0)
#endif

class Reader2 {
public:
    Reader2(const uint8_t* data, size_t size)
        : data_(data), size_(size), pos_(0) {}

    size_t pos() const {
        return pos_;
    }

    size_t remaining() const {
        return pos_ <= size_ ? size_ - pos_ : 0;
    }

    void seek(size_t position) {
        if (position > size_ || (position != 0 && data_ == nullptr)) {
            throw std::runtime_error("IFP seek out of range");
        }

        pos_ = position;
    }

    void skip(size_t count) {
        if (pos_ > size_ || count > size_ - pos_) {
            throw std::runtime_error(
                "IFP skip out of range at offset " +
                std::to_string(pos_) + " (bytes=" + std::to_string(count) + ")"
            );
        }
        pos_ += count;
    }

    void need(size_t count) const {
        if (pos_ > size_ || (count != 0 && data_ == nullptr) || count > size_ - pos_) {
            throw std::runtime_error(
                "Unexpected end of IFP at offset " +
                std::to_string(pos_)
            );
        }
    }

    uint8_t u8() {
        need(1);
        return data_[pos_++];
    }

    uint16_t u16() {
        need(2);

        uint16_t value =
            static_cast<uint16_t>(data_[pos_]) |
            (static_cast<uint16_t>(data_[pos_ + 1]) << 8);

        pos_ += 2;
        return value;
    }

    int16_t i16() {
        return static_cast<int16_t>(u16());
    }

    uint32_t u32() {
        need(4);

        uint32_t value =
            static_cast<uint32_t>(data_[pos_]) |
            (static_cast<uint32_t>(data_[pos_ + 1]) << 8) |
            (static_cast<uint32_t>(data_[pos_ + 2]) << 16) |
            (static_cast<uint32_t>(data_[pos_ + 3]) << 24);

        pos_ += 4;
        return value;
    }

    float f32() {
        uint32_t value = u32();

        float result;
        std::memcpy(&result, &value, sizeof(float));

        return result;
    }

    std::string cc() {
        need(4);

        std::string result(
            reinterpret_cast<const char*>(&data_[pos_]),
            4
        );

        pos_ += 4;
        return result;
    }

    std::string str(uint32_t length) {
        need(length);

        std::string result(
            reinterpret_cast<const char*>(&data_[pos_]),
            length
        );

        pos_ += length;

        const size_t nullPos = result.find('\0');

        if (nullPos != std::string::npos) {
            result.resize(nullPos);
        }

        return result;
    }

    void expect(const char* expected) {
        const std::string actual = cc();

        if (actual != expected) {
            throw std::runtime_error(
                "Expected '" +
                std::string(expected) +
                "', got '" +
                actual +
                "' at offset " +
                std::to_string(pos_ - 4)
            );
        }
    }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_;
};


/*
 * ============================================================
 * Manhunt IFP decoding
 * ============================================================
 *
 * Quaternion:
 *
 *     int16 / 4096.0
 *
 * Translation:
 *
 *     int16 / 2048.0
 *
 * Animation:
 *
 *     30 FPS
 *
 * Frame types:
 *
 *     1 = Quaternion
 *     2 = Quaternion + Translation
 *     3 = Translation
 *
 * ============================================================
 */

static float decodeQuat(int16_t value)
{
    return static_cast<float>(value) / 4096.0f;
}

static float decodeTranslation(int16_t value)
{
    return static_cast<float>(value) / 2048.0f;
}

static float decodeTime(uint16_t value)
{
    return static_cast<float>(value) / 2048.0f;
}


static void readQuaternion(
    Reader2& reader,
    float& x,
    float& y,
    float& z,
    float& w)
{
    x = decodeQuat(reader.i16());
    y = decodeQuat(reader.i16());
    z = decodeQuat(reader.i16());
    w = decodeQuat(reader.i16());
}


static void readTranslation(
    Reader2& reader,
    float& x,
    float& y,
    float& z)
{
    x = decodeTranslation(reader.i16());
    y = decodeTranslation(reader.i16());
    z = decodeTranslation(reader.i16());
}


/*
 * ============================================================
 * Read one SEQU / SEQT
 * ============================================================
 */

static AnimationTrack readTrack(Reader2& reader)
{
    const std::string tag = reader.cc();

    if (tag != "SEQU" && tag != "SEQT") {
        throw std::runtime_error(
            "Invalid sequence tag: " + tag
        );
    }

    AnimationTrack track;

    /*
     * Bone ID
     */
    track.bone_id = reader.u16();

    /*
     * Frame type
     */
    const uint8_t frameType = reader.u8();
    track.frame_type = frameType;

    /*
     * Number of frames
     */
    const uint16_t frameCount = reader.u16();

    if (frameType < 1 || frameType > 3) {
        throw std::runtime_error(
            "Invalid frame type: " +
            std::to_string(frameType)
        );
    }

    /*
     * StartTime
     */
    const uint16_t startTime = reader.u16();

    /*
     * FrameType 3 has an initial quaternion.
     */
    float initialQx = 0.0f;
    float initialQy = 0.0f;
    float initialQz = 0.0f;
    float initialQw = 1.0f;

    if (frameType == 3) {

        // FrameType 3 stores a constant direction quaternion.
        // Direction quaternion uses the same fixed-point quaternion
        // scale as the per-frame rotations.
        initialQx = decodeQuat(reader.i16());
        initialQy = decodeQuat(reader.i16());
        initialQz = decodeQuat(reader.i16());
        initialQw = decodeQuat(reader.i16());

        // There is NO initialization position here.
        // The following bytes belong to the frame timing/data.

    } else {

        /*
         * For sparse tracks, StartTime == 0 means
         * those two bytes are actually the first
         * time delta.
         */
        if (startTime == 0) {
            reader.seek(reader.pos() - 2);
        }
    }

    const size_t min_frame_bytes =
        (frameType == 1) ? 8u : (frameType == 2 ? 14u : 6u);
    if (frameCount > reader.remaining() / min_frame_bytes) {
        throw std::runtime_error(
            "IFP frame count exceeds minimum remaining bytes: " +
            std::to_string(frameCount)
        );
    }
    track.keyframes.reserve(frameCount);

    float currentTime = 0.0f;

    for (uint16_t frame = 0; frame < frameCount; ++frame) {

        /*
         * ----------------------------------------------------
         * Continuous animation
         * ----------------------------------------------------
         *
         * Frame index:
         *
         * StartTime / 2048 * 30 - 1 + frame
         *
         * Therefore:
         *
         * time =
         * StartTime / 2048
         * - 1/30
         * + frame/30
         */
        if (startTime != 0) {

            currentTime =
                static_cast<float>(startTime) / 2048.0f
                - (1.0f / 30.0f)
                + static_cast<float>(frame) / 30.0f;

        } else {

            /*
             * ------------------------------------------------
             * Sparse animation
             * ------------------------------------------------
             */

            if (!(frameType == 3 && frame == 0)) {

                const uint16_t delta = reader.u16();

                currentTime +=
                    static_cast<float>(delta) / 2048.0f;
            }
        }

        AnimationKeyframe keyframe{};

        keyframe.time = currentTime;

        /*
         * ----------------------------------------------------
         * Rotation
         * ----------------------------------------------------
         */

        if (frameType == 1 || frameType == 2) {

            readQuaternion(
                reader,
                keyframe.qx,
                keyframe.qy,
                keyframe.qz,
                keyframe.qw
            );

        } else {

            /*
             * FrameType 3 does not have a quaternion
             * for every frame. Use the sequence's initial
             * direction as the constant orientation.
             */
            keyframe.qx = initialQx;
            keyframe.qy = initialQy;
            keyframe.qz = initialQz;
            keyframe.qw = initialQw;
        }

        /*
         * ----------------------------------------------------
         * Translation
         * ----------------------------------------------------
         */

        if (frameType == 2) {

            readTranslation(
                reader,
                keyframe.tx,
                keyframe.ty,
                keyframe.tz
            );

        } else if (frameType == 3) {

            readTranslation(
                reader,
                keyframe.tx,
                keyframe.ty,
                keyframe.tz
            );

        } else {

            keyframe.tx = 0.0f;
            keyframe.ty = 0.0f;
            keyframe.tz = 0.0f;
        }

        track.keyframes.push_back(keyframe);
    }

    /*
     * SEQT has an additional float.
     *
     * Normal Manhunt 1 animations use SEQU.
     */
    if (tag == "SEQT") {
        reader.f32();
    }

    return track;
}


/*
 * ============================================================
 * Load complete IFP
 * ============================================================
 */

std::map<std::string, Animation> load_ifp(
    const uint8_t* data,
    size_t size)
{
    std::map<std::string, Animation> animations;

    if (data == nullptr || size < 12) {
        LOGE("IFP invalido o demasiado pequeno");
        return animations;
    }


    try {

        Reader2 reader(data, size);

        /*
         * ----------------------------------------------------
         * ANCT
         * ----------------------------------------------------
         */

        reader.expect("ANCT");

        const uint32_t blockCount = reader.u32();
        if (blockCount > reader.remaining() / 16u) {
            throw std::runtime_error(
                "IFP block count exceeds minimum remaining bytes: " +
                std::to_string(blockCount)
            );
        }

        LOGI(
            "IFP: %u bloques encontrados",
            blockCount
        );

        /*
         * ----------------------------------------------------
         * Blocks
         * ----------------------------------------------------
         */

        for (uint32_t blockIndex = 0;
             blockIndex < blockCount;
             ++blockIndex)
        {
            reader.expect("BLOC");

            const uint32_t blockNameLength =
                reader.u32();

            const std::string blockName =
                reader.str(blockNameLength);

            LOGI(
                "Bloque [%u]: %s",
                blockIndex,
                blockName.c_str()
            );

            /*
             * ------------------------------------------------
             * ANPK
             * ------------------------------------------------
             */

            reader.expect("ANPK");

            const uint32_t animationCount =
                reader.u32();
            if (animationCount > reader.remaining() / 16u) {
                throw std::runtime_error(
                    "IFP animation count exceeds minimum remaining bytes: " +
                    std::to_string(animationCount)
                );
            }

            LOGI(
                "Bloque %s: %u animaciones",
                blockName.c_str(),
                animationCount
            );

            /*
             * ------------------------------------------------
             * Animations
             * ------------------------------------------------
             */

            for (uint32_t animationIndex = 0;
                 animationIndex < animationCount;
                 ++animationIndex)
            {
                reader.expect("NAME");

                const uint32_t nameLength =
                    reader.u32();

                Animation animation;

                animation.name =
                    reader.str(nameLength);

                /*
                 * Number of bones/tracks
                 */
                const uint32_t boneCount =
                    reader.u32();
                if (boneCount > reader.remaining() / 11u) {
                    throw std::runtime_error(
                        "IFP track count exceeds minimum remaining bytes: " +
                        std::to_string(boneCount)
                    );
                }

                /*
                 * Chunk size.
                 *
                 * Currently not needed because
                 * tracks are parsed sequentially.
                 */
                const uint32_t chunkSize =
                    reader.u32();

                (void)chunkSize;

                /*
                 * FrameTimesCount
                 */
                const float frameTimesCount =
                    reader.f32();

                animation.duration =
                    frameTimesCount;

                animation.tracks.reserve(
                    boneCount
                );

                /*
                 * ------------------------------------------------
                 * Tracks
                 * ------------------------------------------------
                 */

                for (uint32_t trackIndex = 0;
                     trackIndex < boneCount;
                     ++trackIndex)
                {
                    AnimationTrack track =
                        readTrack(reader);

                    animation.tracks.push_back(
                        std::move(track)
                    );
                }

                /*
                 * ------------------------------------------------
                 * PlayerAnims name dump
                 * ------------------------------------------------
                 */
                if (blockName == "PlayerAnims") {
                    LOGI(
                        "PLAYER_ANIM [%u/%u]: %s",
                        animationIndex + 1,
                        animationCount,
                        animation.name.c_str()
                    );


                }

                /*
                 * ------------------------------------------------
                 * Debug information
                 * ------------------------------------------------
                 */

                if (
                    animation.name == "1st_Person_1Handed_T" ||
                    animation.name == "Stand_Idle"
                ) {

                    LOGI(
                        "================================"
                    );

                    LOGI(
                        "ANIM: %s",
                        animation.name.c_str()
                    );

                    LOGI(
                        "Duration: %.4f",
                        animation.duration
                    );

                    LOGI(
                        "Tracks: %zu",
                        animation.tracks.size()
                    );

                    for (const auto& track :
                         animation.tracks)
                    {
                        LOGI(
                            "Bone=%u Type=%u Keys=%zu InitP=(%.4f %.4f %.4f)",
                            track.bone_id,
                            track.frame_type,
                            track.keyframes.size(),
                            track.initial_tx,
                            track.initial_ty,
                            track.initial_tz
                        );

                        if (!track.keyframes.empty())
                        {
                            const auto& key =
                                track.keyframes.front();

                            LOGI(
                                "  Frame0 "
                                "time=%.4f "
                                "Q=(%.4f %.4f %.4f %.4f) "
                                "P=(%.4f %.4f %.4f)",
                                key.time,

                                key.qx,
                                key.qy,
                                key.qz,
                                key.qw,

                                key.tx,
                                key.ty,
                                key.tz
                            );
                        }
                    }

                    LOGI(
                        "================================"
                    );
                }

                /*
                 * ------------------------------------------------
                 * Particle / effect section
                 * ------------------------------------------------
                 */

                const uint32_t headerSize =
                    reader.u32();

                if (headerSize != 0x10) {

                    throw std::runtime_error(
                        "Invalid particle section header "
                        "in animation " +
                        animation.name
                    );
                }

                /*
                 * Unknown
                 */
                reader.f32();

                /*
                 * Entry size
                 */
                const uint32_t entrySize =
                    reader.u32();

                /*
                 * Number of entries
                 */
                const uint32_t entryCount =
                    reader.u32();

                if (entryCount > 0) {

                    const uint64_t totalSize =
                        static_cast<uint64_t>(entrySize) *
                        static_cast<uint64_t>(entryCount);

                    if (totalSize >
                        static_cast<uint64_t>(size - reader.pos()))
                    {
                        throw std::runtime_error(
                            "Particle section exceeds IFP size"
                        );
                    }

                    reader.skip(
                        static_cast<size_t>(totalSize)
                    );
                }

                /*
                 * ------------------------------------------------
                 * Validacion de tiempo
                 * ------------------------------------------------
                 */
                for (const auto& track : animation.tracks) {
                    for (size_t k = 0; k + 1 < track.keyframes.size(); ++k) {
                        if (track.keyframes[k].time > track.keyframes[k+1].time + 0.0001f) {
                            LOGE("IFP TIEMPO INVALIDO en %s bone %u: frame %zu(%.4f) > frame %zu(%.4f)", 
                                 animation.name.c_str(), track.bone_id, k, track.keyframes[k].time, k+1, track.keyframes[k+1].time);
                        }
                    }
                }

                /*
                 * ------------------------------------------------
                 * Store animation
                 * ------------------------------------------------
                 */

                animations[animation.name] =
                    std::move(animation);
            }

        }

        /*
         * ----------------------------------------------------
         * Finished
         * ----------------------------------------------------
         */

        __android_log_print(
            ANDROID_LOG_INFO, LOG_TAG,
            "IFP cargado: %zu animaciones", animations.size()
        );


    }
    catch (const std::exception& e) {

        LOGE(
            "ERROR parseando IFP: %s",
            e.what()
        );
        animations.clear();
    }

    return animations;
}