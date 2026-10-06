import sys

with open('C:/Users/Administrator/Documents/Port/app/src/main/cpp/native-lib.cpp', 'r', encoding='utf-8') as f:
    lines = f.readlines()

start_idx = -1
end_idx = -1

for i, line in enumerate(lines):
    if '// ── Skeletal Animation Update' in line:
        start_idx = i
        break

if start_idx != -1:
    for i in range(start_idx, len(lines)):
        if '// Joystick de movimiento' in lines[i]:
            # we want to keep the glBindVertexArray(0); which is usually right before Joystick
            end_idx = i
            break

if start_idx != -1 and end_idx != -1:
    # also remove the glBindVertexArray(0); right before end_idx to replace it cleanly
    while 'glBindVertexArray(0);' in lines[end_idx-1] or lines[end_idx-1].strip() == '' or '}' in lines[end_idx-1]:
        end_idx -= 1
        
    new_code = """    // ── Skeletal Animation Update ─────────────────────────────────────────────
    g_anim_time += dt;
    auto it_cash = g_model_render.find("cash");
    if (it_cash != g_model_render.end()) {
        Mat4 global_bones[96];
        Mat4 skin_matrices[96];
        for (int i = 0; i < 96; ++i) {
            global_bones[i] = mat4_identity();
            skin_matrices[i] = mat4_identity();
        }
        /*
         * --------------------------------------------------------
         * Seleccionar animación
         * --------------------------------------------------------
         */
        const Animation* anim = nullptr;
        float speed = sqrtf(
            g_move_fwd * g_move_fwd +
            g_move_right * g_move_right
        );
        auto find_anim = [&](const char* wanted) -> const Animation* {
            std::string query = to_lower(wanted);
            for (const auto& pair : g_anims) {
                std::string name = to_lower(pair.first);
                if (name == query) {
                    return &pair.second;
                }
            }
            return nullptr;
        };
        /*
         * Primero intentamos encontrar las animaciones exactas.
         */
        if (speed > 0.6f) {
            anim = find_anim("Run_Fwd");
        } else if (speed > 0.05f) {
            anim = find_anim("Walk_Fwd");
        } else {
            anim = find_anim("Stand_Idle");
        }
        /*
         * Si no existen esas animaciones, usamos Stand_Idle.
         */
        if (!anim) {
            anim = find_anim("Stand_Idle");
        }
        /*
         * Último fallback.
         */
        if (!anim && !g_anims.empty()) {
            anim = &g_anims.begin()->second;
        }
        /*
         * --------------------------------------------------------
         * Debug de animación
         * --------------------------------------------------------
         */
        static bool logged_animation = false;
        if (!logged_animation && anim) {
            LOGI(
                "ANIM PLAY: %s duration=%.3f tracks=%zu",
                anim->name.c_str(),
                anim->duration,
                anim->tracks.size()
            );
            logged_animation = true;
        }
        /*
         * --------------------------------------------------------
         * Tiempo de animación
         * --------------------------------------------------------
         */
        float animation_time = 0.0f;
        if (anim && anim->duration > 0.0f) {
            animation_time = fmodf(
                g_anim_time,
                anim->duration
            );
            if (animation_time < 0.0f)
                animation_time += anim->duration;
        }
        /*
         * --------------------------------------------------------
         * Procesar huesos
         * --------------------------------------------------------
         */
        for (
            size_t bone_idx = 0;
            bone_idx < g_cash_model.bones.size() && bone_idx < 96;
            ++bone_idx
        ) {
            const DFFBone& bone = g_cash_model.bones[bone_idx];
            /*
             * ----------------------------------------------------
             * Pose base
             * ----------------------------------------------------
             */
            float pos[3] = { bone.pos_x, bone.pos_y, bone.pos_z };
            float quat[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            bool animated = false;
            /*
             * ----------------------------------------------------
             * Buscar track usando SOLAMENTE bone_id
             * ----------------------------------------------------
             *
             * No usamos bone_idx como fallback.
             *
             * El IFP utiliza IDs como:
             *
             * 1000
             * 1001
             * 1003
             * 1045
             * 1095
             *
             * Por lo tanto hay que relacionarlos con el
             * bone_id real del DFF.
             */
            if (
                anim &&
                bone.bone_id != 0xFFFFFFFF
            ) {
                for (const auto& track : anim->tracks) {
                    if (
                        track.bone_id != static_cast<int>(bone.bone_id)
                    ) {
                        continue;
                    }
                    if (track.keyframes.empty()) {
                        continue;
                    }
                    animated = true;
                    /*
                     * ------------------------------------------------
                     * Buscar los dos keyframes
                     * ------------------------------------------------
                     */
                    const AnimationKeyframe* k0 = &track.keyframes.front();
                    const AnimationKeyframe* k1 = &track.keyframes.front();
                    /*
                     * Antes del primer frame.
                     */
                    if (
                        animation_time <= track.keyframes.front().time
                    ) {
                        k0 = &track.keyframes.front();
                        k1 = &track.keyframes.front();
                    } else {
                        bool found = false;
                        for (
                            size_t k = 0;
                            k + 1 < track.keyframes.size();
                            ++k
                        ) {
                            const auto& a = track.keyframes[k];
                            const auto& b = track.keyframes[k + 1];
                            if (
                                animation_time >= a.time &&
                                animation_time <= b.time
                            ) {
                                k0 = &a;
                                k1 = &b;
                                found = true;
                                break;
                            }
                        }
                        /*
                         * Si estamos después del último keyframe,
                         * mantenemos el último.
                         */
                        if (!found) {
                            k0 = &track.keyframes.back();
                            k1 = &track.keyframes.back();
                        }
                    }
                    /*
                     * ------------------------------------------------
                     * Interpolación
                     * ------------------------------------------------
                     */
                    float t = 0.0f;
                    if (
                        k1 != k0 &&
                        k1->time > k0->time
                    ) {
                        t = (animation_time - k0->time) / (k1->time - k0->time);
                        t = std::max(
                            0.0f,
                            std::min(1.0f, t)
                        );
                    }
                    /*
                     * ------------------------------------------------
                     * Quaternion
                     * ------------------------------------------------
                     */
                    float qx1 = k1->qx;
                    float qy1 = k1->qy;
                    float qz1 = k1->qz;
                    float qw1 = k1->qw;
                    /*
                     * Quaternion shortest path.
                     */
                    float dot = k0->qx * qx1 + k0->qy * qy1 + k0->qz * qz1 + k0->qw * qw1;
                    if (dot < 0.0f) {
                        qx1 = -qx1;
                        qy1 = -qy1;
                        qz1 = -qz1;
                        qw1 = -qw1;
                    }
                    quat[0] = k0->qx + t * (qx1 - k0->qx);
                    quat[1] = k0->qy + t * (qy1 - k0->qy);
                    quat[2] = k0->qz + t * (qz1 - k0->qz);
                    quat[3] = k0->qw + t * (qw1 - k0->qw);
                    /*
                     * Normalizar.
                     */
                    float qlen = sqrtf(
                        quat[0] * quat[0] +
                        quat[1] * quat[1] +
                        quat[2] * quat[2] +
                        quat[3] * quat[3]
                    );
                    if (qlen > 0.000001f) {
                        quat[0] /= qlen;
                        quat[1] /= qlen;
                        quat[2] /= qlen;
                        quat[3] /= qlen;
                    }
                    /*
                     * ------------------------------------------------
                     * Translation
                     * ------------------------------------------------
                     */
                    pos[0] = k0->tx + t * (k1->tx - k0->tx);
                    pos[1] = k0->ty + t * (k1->ty - k0->ty);
                    pos[2] = k0->tz + t * (k1->tz - k0->tz);
                    /*
                     * Debug solamente para algunos huesos.
                     */
                    static int debug_anim_counter = 0;
                    if (
                        debug_anim_counter++ % 120 == 0 &&
                        (
                            bone.bone_id == 1000 ||
                            bone.bone_id == 1045 ||
                            bone.bone_id == 1095
                        )
                    ) {
                        LOGI(
                            "ANIM APPLY: anim=%s bone=%u "
                            "time=%.3f "
                            "Q=(%.3f %.3f %.3f %.3f) "
                            "P=(%.3f %.3f %.3f)",
                            anim->name.c_str(),
                            bone.bone_id,
                            animation_time,
                            quat[0], quat[1], quat[2], quat[3],
                            pos[0], pos[1], pos[2]
                        );
                    }
                    break;
                }
            }
            /*
             * --------------------------------------------------------
             * Matriz bind
             * --------------------------------------------------------
             */
            Mat4 bind_mat = mat4_identity();
            bind_mat.m[0] = bone.rot_mat[0];
            bind_mat.m[1] = bone.rot_mat[1];
            bind_mat.m[2] = bone.rot_mat[2];
            bind_mat.m[4] = bone.rot_mat[3];
            bind_mat.m[5] = bone.rot_mat[4];
            bind_mat.m[6] = bone.rot_mat[5];
            bind_mat.m[8] = bone.rot_mat[6];
            bind_mat.m[9] = bone.rot_mat[7];
            bind_mat.m[10] = bone.rot_mat[8];
            bind_mat.m[12] = bone.pos_x;
            bind_mat.m[13] = bone.pos_y;
            bind_mat.m[14] = bone.pos_z;
            /*
             * --------------------------------------------------------
             * Matriz local
             * --------------------------------------------------------
             */
            Mat4 local_mat = bind_mat;
            if (animated) {
                /*
                 * La animación proporciona la rotación.
                 * La posición del hueso se conserva desde el DFF,
                 * excepto cuando el track proporciona traslación.
                 */
                float zero[3] = { 0.0f, 0.0f, 0.0f };
                Mat4 anim_mat = mat4_from_pos_quat(
                    zero,
                    quat
                );
                local_mat = anim_mat;
                /*
                 * Mantener la posición del hueso
                 * proveniente del skeleton.
                 */
                local_mat.m[12] = bind_mat.m[12];
                local_mat.m[13] = bind_mat.m[13];
                local_mat.m[14] = bind_mat.m[14];
                /*
                 * Root.
                 *
                 * Si es el root, sí usamos la traslación
                 * proporcionada por la animación.
                 */
                if (bone.bone_id == 1000) {
                    local_mat.m[12] = pos[0];
                    local_mat.m[13] = pos[1];
                    local_mat.m[14] = pos[2];
                }
            }
            /*
             * --------------------------------------------------------
             * Jerarquía
             * --------------------------------------------------------
             */
            if (
                bone.parent != 0xFFFFFFFF &&
                bone.parent < bone_idx
            ) {
                global_bones[bone_idx] = mat4_mul(
                    global_bones[bone.parent],
                    local_mat
                );
            } else {
                global_bones[bone_idx] = local_mat;
            }
            /*
             * --------------------------------------------------------
             * Skin matrix
             * --------------------------------------------------------
             */
            if (
                bone.matrix_index != 0xFFFFFFFF &&
                bone.matrix_index < 96 &&
                (
                    bone.matrix_index * 16 + 15
                ) < g_cash_model.inverse_bind_matrices.size()
            ) {
                Mat4 inverse_bind;
                for (int j = 0; j < 16; ++j) {
                    inverse_bind.m[j] = g_cash_model
                        .inverse_bind_matrices[
                            static_cast<size_t>(
                                bone.matrix_index
                            ) * 16 + j
                        ];
                }
                skin_matrices[
                    bone.matrix_index
                ] = mat4_mul(
                    global_bones[bone_idx],
                    inverse_bind
                );
            }
        }
        /*
         * --------------------------------------------------------
         * Render
         * --------------------------------------------------------
         */
        Vec3 center_pos = vec3_add(
            g_player_pos,
            { 0.0f, g_cash_y_offset, 0.0f }
        );
        Mat4 cash_model_m = mat4_from_pos_yaw(
            center_pos,
            g_player_yaw
        );
        Mat4 cash_mvp = mat4_mul(
            vp,
            cash_model_m
        );
        glUniformMatrix4fv(
            loc_mvp,
            1,
            GL_FALSE,
            cash_mvp.m
        );
        glUniformMatrix4fv(
            loc_model,
            1,
            GL_FALSE,
            cash_model_m.m
        );
        glUniform1i(
            loc_skinned,
            1
        );
        glUniformMatrix4fv(
            loc_bones,
            96,
            GL_FALSE,
            skin_matrices[0].m
        );
        glBindVertexArray(
            it_cash->second.vao
        );
        for (const auto& group : it_cash->second.groups) {
            if (group.texture_id) {
                glBindTexture(
                    GL_TEXTURE_2D,
                    group.texture_id
                );
            }
            glBindBuffer(
                GL_ELEMENT_ARRAY_BUFFER,
                group.ebo
            );
            glDrawElements(
                GL_TRIANGLES,
                group.num_indices,
                GL_UNSIGNED_SHORT,
                nullptr
            );
        }
    }
    glBindVertexArray(0);
"""

    lines = lines[:start_idx] + [new_code + '\n'] + lines[end_idx:]
    with open('C:/Users/Administrator/Documents/Port/app/src/main/cpp/native-lib.cpp', 'w', encoding='utf-8') as f:
        f.writelines(lines)
    print('Done!')
else:
    print('Failed to find markers!', start_idx, end_idx)
