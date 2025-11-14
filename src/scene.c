#include "scene.h"

#include "alloc.h"
#include "assets.h"
#include "box.h"
#include "color.h"
#include "macros.h"
#include "triangle.h"

static inline void
transform_default(struct transform *dest) {
    dest->pos = (vec3){0};
    dest->rot = mat3_identity();
    dest->scale = 1.0f;
}

static void
scene_node_init(struct scene_node *node, struct scene_tree *parent, enum scene_node_type type) {
    node->parent = parent;
    node->type = type;
    transform_default(&node->transform);

    if(parent) {
        scene_node_ptr_array_push(&parent->children, node);
    }
}

struct scene_polygon *
scene_add_polygon(struct scene_tree *parent, int len, vec3 vertices[len]) {
    struct scene_polygon *scene_polygon = alloc(sizeof(*scene_polygon));
    todo("add polygon to triangles conversion");

    scene_node_init(&scene_polygon->node, parent, SCENE_NODE_TYPE_POLYGON);

    return scene_polygon;
}

struct scene_mesh *
scene_add_mesh(struct scene_tree *parent, struct mesh *mesh) {
    struct scene_mesh *scene_mesh = alloc(sizeof(*scene_mesh));
    scene_mesh->mesh = mesh;

    scene_node_init(&scene_mesh->node, parent, SCENE_NODE_TYPE_MESH);

    return scene_mesh;
}

struct scene_tree *
scene_add_tree(struct scene_tree *parent) {
    struct scene_tree *scene_tree = alloc(sizeof(*scene_tree));

    scene_node_init(&scene_tree->node, parent, SCENE_NODE_TYPE_TREE);

    return scene_tree;
}

void
scene_node_set_position(struct scene_node *node, vec3 pos) {
    node->transform.pos = pos;
}

static inline mat3
get_rotation_matrix(vec3 rot) {
    return mat3_mul(mat3_mul(mat3_rotation_z(rot.z), mat3_rotation_x(rot.x)), mat3_rotation_y(rot.y));
}

void
scene_node_set_rotation(struct scene_node *node, vec3 rot) {
    node->transform.rot = get_rotation_matrix(rot);
}

void
scene_node_set_scale(struct scene_node *node, float scale) {
    node->transform.scale = scale;
}

static void
remove_node_from_parents_children(struct scene_node *node) {
    for(int i = 0; i < node->parent->children.len; i++) {
        if(node->parent->children.data[i] == node) {
            scene_node_ptr_array_remove_fast(&node->parent->children, i);
            return;
        }
    }
}

void
scene_node_reparent(struct scene_node *node, struct scene_tree *parent) {
    if(node->parent) {
        remove_node_from_parents_children(node);
    }

    node->parent = parent;
    if(parent) {
        scene_node_ptr_array_push(&parent->children, node);
    }
}

static void
scene_node_remove_iter(struct scene_node *node) {
    switch(node->type) {
        case SCENE_NODE_TYPE_TREE: {
            struct scene_tree *tree = container_of(node, struct scene_tree, node);
            for(struct scene_node **iter = tree->children.data; iter < scene_node_ptr_array_end(&tree->children);
                    iter++) {
                scene_node_remove_iter(*iter);
            }
            scene_node_ptr_array_deinit(&tree->children);
            free(tree);
            break;
        }
        case SCENE_NODE_TYPE_POLYGON: {
            struct scene_polygon *polygon = container_of(node, struct scene_polygon, node);
            todo("free the polygon struct fields");
            free(polygon);
            break;
        }
        case SCENE_NODE_TYPE_MESH: {
            struct scene_mesh *mesh = container_of(node, struct scene_mesh, node);
            free(mesh);
            break;
        }
    }
}

void
scene_node_remove(struct scene_node *node) {
    if(node->parent) {
        remove_node_from_parents_children(node);
    }

    // this will remove the object nodes, and iteratively remove the tree's children and childrens children etc
    scene_node_remove_iter(node);
}

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

    dest->x = (x + 1.0f) * 0.5f * camera->width;
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

static void
render_face(struct face_render_data *face, struct camera *camera, struct transform *transform, u32 *buffer,
        float *depth_buffer) {
    face_transform(face, transform);

    vec2 proj[3];
    float depths[3];
    for(int i = 0; i < 3; i++) {
        depths[i] = project_point(camera, face->vertices[i].vertex, &proj[i]);
        if(depths[i] <= 0.0f) {
            return;
        }
    }

    if(triangle_signed_area(proj[0], proj[1], proj[2]) >= 0) {
        return;
    }

    struct bounding_box box = triangle_get_bounding_box(proj[0], proj[1], proj[2]);

    for(int x = max(box.start_x, 0); x < min(camera->width, box.end_x); x++) {
        for(int y = max(box.start_y, 0); y < min(box.end_y, camera->height); y++) {
            vec2 p = {x + 0.5f, y + 0.5f};

            float alpha, beta, gamma;
            get_barycentric_coords(proj, p, &alpha, &beta, &gamma);

            if(alpha >= 0 && beta >= 0 && gamma >= 0) {
                float depth = alpha * depths[0] + beta * depths[1] + gamma * depths[2];

                int index = camera_to_buffer_coords(camera, x, y);
                if(depth < depth_buffer[index]) {
                    depth_buffer[index] = depth;

                    // calculate the color by interpolating normals
                    if(face->has_normals) {
                        vec3 normal = vec3_normalize(vec3_add(vec3_add(vec3_scale(alpha, face->vertices[0].normal),
                                                                      vec3_scale(beta, face->vertices[1].normal)),
                                vec3_scale(gamma, face->vertices[2].normal)));

                        vec3 weights = vec3_scale(0.5f, vec3_add(normal, (vec3){1.0f, 1.0f, 1.0f}));
                        buffer[index] = color_pack(255, 255 * weights.x, 255 * weights.y, 255 * weights.z);
                    } else {
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
scene_render_iter(struct scene_tree *tree, struct camera *camera, struct transform *transform, u32 *buffer,
        float *depth_buffer) {
    for(struct scene_node **node = tree->children.data; node < scene_node_ptr_array_end(&tree->children); node++) {
        struct transform current_transform = (*node)->transform;
        transform_add(&current_transform, transform);

        switch((*node)->type) {
            case SCENE_NODE_TYPE_MESH: {
                struct scene_mesh *mesh = container_of((*node), struct scene_mesh, node);

                struct face_render_data data;
                for(int i = 0; i < mesh->mesh->faces.len; i++) {
                    face_get_render_data(mesh->mesh, i, &data);
                    // static bool has = false;
                    // if(!has) {
                    //     printf("(%f, %f, %f), (%f, %f, %f), (%f, %f, %f)\n", data.vertices[0].vertex.x,
                    //             data.vertices[0].vertex.y, data.vertices[0].vertex.z, data.vertices[1].vertex.x,
                    //             data.vertices[1].vertex.y, data.vertices[1].vertex.z, data.vertices[2].vertex.x,
                    //             data.vertices[2].vertex.y, data.vertices[2].vertex.z);
                    //     has = true;
                    // }
                    render_face(&data, camera, &current_transform, buffer, depth_buffer);
                }
                break;
            }
            case SCENE_NODE_TYPE_POLYGON: {
                todo("implement polygon rendering");
                break;
            }
            case SCENE_NODE_TYPE_TREE: {
                tree = container_of((*node), struct scene_tree, node);
                scene_render_iter(tree, camera, &current_transform, buffer, depth_buffer);
                break;
            }
        }
    }
}

void
scene_render(struct scene_tree *scene, struct camera *camera, u32 *buffer, float *depth_buffer) {
    // reset the buffers
    for(int x = 0; x < camera->width; x++) {
        for(int y = 0; y < camera->height; y++) {
            buffer[y * camera->width + x] = 0xff000000;
            depth_buffer[y * camera->width + x] = INFINITY;
        }
    }

    // render it with initial params
    struct transform transform;
    transform_default(&transform);

    scene_render_iter(scene, camera, &transform, buffer, depth_buffer);
}
