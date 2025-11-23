#include "render.h"

#include <stdio.h>

#include "assets.h"
#include "box.h"
#include "camera.h"
#include "color.h"
#include "macros.h"
#include "triangle.h"
#include "vec2.h"

static float
project_point(struct camera *camera, vec3 point, vec2 *dest) {
    vec3 rel = vec3_sub(point, camera->pos);
    float depth = vec3_dot(rel, camera->normal);

    if(depth <= 0.0f) {
        return depth;
    }

    float f = 1.0f / tanf(camera->fov * 0.5f);

    float x = vec3_dot(rel, camera->right) / depth;
    float y = vec3_dot(rel, camera->up) / depth;

    x *= f / ((float)camera->width / camera->height);
    y *= f;

    // transform it from (-1, 1] to (0, 1] and then to width x height box coords
    dest->x = (x + 1.0f) * 0.5f * camera->width;
    // for y we also invert it so it coresponds to the buffer coordinates instead
    dest->y = (1.0f - (y + 1.0f) * 0.5f) * camera->height;

    return depth;
}

static inline int
camera_to_buffer_coords(struct camera *camera, int x, int y) {
    return y * camera->width + x;
}

struct face_render_data {
    struct vertex_render_data {
        vec3 vertex;
        vec3 normal;
        vec2 texture;
    } vertices[3];

    bool has_normals;
    bool has_textures;
};

static void
face_get_render_data(struct mesh *mesh, int index, struct face_render_data *dest) {
    dest->has_normals = true;
    dest->has_textures = true;

    struct face *face = &mesh->faces.data[index];
    for(int i = 0; i < 3; i++) {
        int j = face->vertices[i].vertex_index;
        dest->vertices[i].vertex = mesh->vertices.data[j];

        j = face->vertices[i].normal_index;
        if(j >= 0) {
            dest->vertices[i].normal = mesh->normals.data[j];
        } else {
            dest->has_normals = false;
        }

        j = face->vertices[i].texture_index;
        if(j >= 0) {
            dest->vertices[i].texture = mesh->textures.data[j];
        } else {
            dest->has_textures = false;
        }
    }
}

static inline void
face_transform(struct face_render_data *face, struct transform *transform) {
    vec3 pos = transform->pos;
    float scale = transform->scale;
    mat3 rot = transform->rot;

    face->vertices[0].vertex = mat3_mul_vec3(rot, face->vertices[0].vertex);
    face->vertices[1].vertex = mat3_mul_vec3(rot, face->vertices[1].vertex);
    face->vertices[2].vertex = mat3_mul_vec3(rot, face->vertices[2].vertex);

    // also rotate the normals
    if(face->has_normals) {
        face->vertices[0].normal = mat3_mul_vec3(rot, face->vertices[0].normal);
        face->vertices[1].normal = mat3_mul_vec3(rot, face->vertices[1].normal);
        face->vertices[2].normal = mat3_mul_vec3(rot, face->vertices[2].normal);
    }

    // and then scale and translate the positions
    face->vertices[0].vertex = vec3_add(vec3_scale(scale, face->vertices[0].vertex), pos);
    face->vertices[1].vertex = vec3_add(vec3_scale(scale, face->vertices[1].vertex), pos);
    face->vertices[2].vertex = vec3_add(vec3_scale(scale, face->vertices[2].vertex), pos);
}

static inline void
get_barycentric_coords(vec2 t[3], vec2 p, float *alpha, float *beta, float *gamma) {
    float area = triangle_signed_area(t[0], t[1], t[2]);

    *alpha = triangle_signed_area(t[1], t[2], p) / area;  // bcp
    *beta = triangle_signed_area(t[2], t[0], p) / area;  // cap
    *gamma = triangle_signed_area(t[0], t[1], p) / area;  // abp
}

static inline vec3
texture_get_color(struct texture *texture, float u, float v) {
    int x = u * (texture->width - 1);
    // invert the y axis
    int y = (1 - v) * (texture->height - 1);

    int index = (y * texture->width + x) * 4;
    u8 r = texture->pixels[index];
    u8 g = texture->pixels[index + 1];
    u8 b = texture->pixels[index + 2];

    return (vec3){r / 255.0f, g / 255.0f, b / 255.0f};
}

static void
render_face(struct face_render_data *face, struct camera *camera, struct transform *transform,
        struct material *material, u32 *buffer, float *depth_buffer) {
    face_transform(face, transform);

    vec2 proj[3];
    float depths[3];
    for(int i = 0; i < 3; i++) {
        depths[i] = project_point(camera, face->vertices[i].vertex, &proj[i]);
        if(depths[i] <= 0.0f) {
            return;
        }
    }

    // skip backfaces
    if(triangle_signed_area(proj[0], proj[1], proj[2]) >= 0) {
        return;
    }

    struct bounding_box box = triangle_get_bounding_box(proj[0], proj[1], proj[2]);

    for(int x = max(box.start_x, 0); x < min(box.end_x, camera->width); x++) {
        for(int y = max(box.start_y, 0); y < min(box.end_y, camera->height); y++) {
            vec2 p = {x + 0.5f, y + 0.5f};

            float alpha, beta, gamma;
            get_barycentric_coords(proj, p, &alpha, &beta, &gamma);

            if(alpha >= 0 && beta >= 0 && gamma >= 0) {
                float depth = alpha * depths[0] + beta * depths[1] + gamma * depths[2];

                int index = camera_to_buffer_coords(camera, x, y);
                if(depth < depth_buffer[index]) {
                    depth_buffer[index] = depth;

                    if(face->has_textures && material) {
                        // white light
                        vec3 color = {1.0f, 1.0f, 1.0f};
                        if(material->texture) {
                            // sample the texture
                            float u0 = face->vertices[0].texture.x;
                            float u1 = face->vertices[1].texture.x;
                            float u2 = face->vertices[2].texture.x;
                            float v0 = face->vertices[0].texture.y;
                            float v1 = face->vertices[1].texture.y;
                            float v2 = face->vertices[2].texture.y;

                            float denom = (alpha / depths[0] + beta / depths[1] + gamma / depths[2]);
                            if(fequal(denom, 0.0f)) {
                                // do anything
                                denom = 1.0f;
                            }

                            float u = (alpha * u0 / depths[0] + beta * u1 / depths[1] + gamma * u2 / depths[2]) / denom;
                            float v = (alpha * v0 / depths[0] + beta * v1 / depths[1] + gamma * v2 / depths[2]) / denom;

                            u = clamp(u, 0.0f, 1.0f);
                            v = clamp(v, 0.0f, 1.0f);

                            vec3 pixel = texture_get_color(material->texture, u, v);
                            color.x *= pixel.x;
                            color.y *= pixel.y;
                            color.z *= pixel.z;
                        }

                        color.x *= material->diffuse_color.x;
                        color.y *= material->diffuse_color.y;
                        color.z *= material->diffuse_color.z;

                        if(face->has_normals) {
                            vec3 normal = vec3_normalize(vec3_add(vec3_add(vec3_scale(alpha, face->vertices[0].normal),
                                                                          vec3_scale(beta, face->vertices[1].normal)),
                                    vec3_scale(gamma, face->vertices[2].normal)));

                            // vec3 light_source = vec3_scale(-1, camera->normal);
                            // vec3 light_source = {1 / sqrtf(3), 1 / sqrtf(3), 1 / sqrtf(3)};
                            vec3 light_source = {-1 / sqrtf(2), -1 / sqrtf(2), 0.0f};

                            float direction_factor = max(vec3_dot(normal, light_source), 0.2f);
                            // printf("%f, %f, %f,    %f\n", normal.x, normal.y, normal.z, direction_factor);
                            color = vec3_scale(direction_factor, color);
                        }

                        buffer[index] = color_pack(255, 255 * color.x, 255 * color.y, 255 * color.z);
                    } else {
                        // else just draw it in cyan
                        buffer[index] = 0xff00ffff;
                    }
                }
            }
        }
    }
}

static void
transform_add(struct transform *dest, struct transform *other) {
    dest->pos = vec3_add(other->pos, dest->pos);
    dest->rot = mat3_mul(other->rot, dest->rot);
    dest->scale *= other->scale;
}

static void
render_iter(struct scene_tree *tree, struct camera *camera, struct transform *transform, u32 *buffer,
        float *depth_buffer) {
    for(struct scene_node **node = tree->children.data; node < scene_node_ptr_array_end(&tree->children); node++) {
        struct transform current_transform = (*node)->transform;
        transform_add(&current_transform, transform);

        switch((*node)->type) {
            case SCENE_NODE_TYPE_MESH: {
                struct scene_mesh *mesh = container_of((*node), struct scene_mesh, node);

                struct use_material *current_material = NULL;
                struct use_material *next_material =
                        mesh->mesh->use_materials.len != 0 ? &mesh->mesh->use_materials.data[0] : NULL;

                struct face_render_data data;
                for(int i = 0; i < mesh->mesh->faces.len; i++) {
                    if(next_material && next_material->face_index == i) {
                        current_material = next_material;
                        next_material = current_material + 1;
                        if(next_material == use_material_array_end(&mesh->mesh->use_materials)) {
                            next_material = NULL;
                        }
                    }

                    face_get_render_data(mesh->mesh, i, &data);
                    render_face(&data, camera, &current_transform, current_material->material, buffer, depth_buffer);
                }
                break;
            }
            case SCENE_NODE_TYPE_POLYGON: {
                todo("implement polygon rendering");
                break;
            }
            case SCENE_NODE_TYPE_TREE: {
                tree = container_of((*node), struct scene_tree, node);

                render_iter(tree, camera, &current_transform, buffer, depth_buffer);
                break;
            }
        }
    }
}

void
render(struct scene_tree *scene, struct camera *camera, u32 *buffer, float *depth_buffer) {
    // reset the buffers
    for(int x = 0; x < camera->width; x++) {
        for(int y = 0; y < camera->height; y++) {
            buffer[y * camera->width + x] = 0xff87ceeb;
            depth_buffer[y * camera->width + x] = INFINITY;
        }
    }

    // render it with initial params
    struct transform transform;
    transform_default(&transform);

    render_iter(scene, camera, &transform, buffer, depth_buffer);
}
