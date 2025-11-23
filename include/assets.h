#ifndef MESH_H
#define MESH_H

#include "array.h"
#include "dynamic_string.h"
#include "ints.h"
#include "list.h"
#include "vec2.h"
#include "vec3.h"

struct vertex {
    int vertex_index, normal_index, texture_index;
};

struct face {
    struct vertex vertices[3];
};

struct texture {
    int width, height;
    // this is how `stb_image` loads them, so we keep in this format of ARGB values
    u8* pixels;

    // we keep the file path here so we can reuse the material for multiple objects
    string_t path;
    // we also keep the ref count so we know when to drop it
    int ref_count;

    list_t link;
};

struct material {
    string_t name;

    vec3 diffuse_color;
    vec3 specular_color;
    vec3 ambient_color;
    float shininess;
    float opacity;
    int illumination_model;

    // may be NULL
    struct texture* texture;

    // since a single mesh can have mulitple materials we keep all of them linked
    list_t link;
};

struct use_material {
    int face_index;
    struct material* material;
};

define_array(struct face, face_array);
define_array(vec2, vec2_array);
define_array(vec3, vec3_array);
define_array(struct use_material, use_material_array);

struct mesh {
    vec3_array_t vertices;
    vec3_array_t normals;
    vec2_array_t textures;

    face_array_t faces;

    list_t materials;
    use_material_array_t use_materials;

    string_t path;
    list_t link;
};

struct assets_manager {
    list_t meshes, textures;
};

struct assets_manager*
assets_manager_create(void);

void
assets_manager_destroy(struct assets_manager* manager);

struct mesh*
assets_manager_load_mesh(struct assets_manager* manager, char* path);

void
mesh_destroy(struct mesh* mesh);

#endif
