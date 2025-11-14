#include "assets.h"

#include <sys/stat.h>

#include "alloc.h"
#include "ints.h"
#include "reader.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

struct assets_manager *
assets_manager_create(void) {
    struct assets_manager *manager = alloc(sizeof(*manager));
    list_init(&manager->meshes);
    list_init(&manager->textures);

    return manager;
}

void
assets_manager_destroy(struct assets_manager *manager) {
    // we only need to destroy the meshes, since they will unref the textures, effectively dropping all of them
    list_for_each_safe(struct mesh, iter, &manager->meshes, link) {
        mesh_destroy(iter);
    }

    free(manager);
}

static bool
mesh_add_vertex(struct mesh *mesh, string_array_t *parts) {
    if(parts->len < 4) {
        return false;
    }

    vec3_array_push(&mesh->vertices,
            (vec3){
                    atof(string_c_string_view(&parts->data[1])),
                    atof(string_c_string_view(&parts->data[2])),
                    atof(string_c_string_view(&parts->data[3])),
            });

    return true;
}

static bool
mesh_add_normal(struct mesh *mesh, string_array_t *parts) {
    if(parts->len < 4) {
        return false;
    }

    vec3_array_push(&mesh->normals,
            (vec3){
                    atof(string_c_string_view(&parts->data[1])),
                    atof(string_c_string_view(&parts->data[2])),
                    atof(string_c_string_view(&parts->data[3])),
            });

    return true;
}

static bool
mesh_add_texture(struct mesh *mesh, string_array_t *parts) {
    if(parts->len < 3) {
        return false;
    }

    vec2_array_push(&mesh->textures,
            (vec2){
                    atof(string_c_string_view(&parts->data[1])),
                    atof(string_c_string_view(&parts->data[2])),
            });

    return true;
}

static bool
parse_face_vertex(string_t *s, struct vertex *dest) {
    string_array_t parts = {0};
    string_split(s, '/', false, &parts);

    bool ret;
    if(parts.len == 1) {
        dest->vertex_index = atoi(string_c_string_view(&parts.data[0])) - 1;
        dest->texture_index = -1;
        dest->normal_index = -1;

        ret = true;
    } else if(parts.len == 3) {
        dest->vertex_index = atoi(string_c_string_view(&parts.data[0])) - 1;
        dest->texture_index = atoi(string_c_string_view(&parts.data[1])) - 1;
        dest->normal_index = atoi(string_c_string_view(&parts.data[2])) - 1;

        ret = true;
    } else {
        ret = false;
    }

    for(struct string *iter = parts.data; iter < string_array_end(&parts); iter++) {
        string_deinit(iter);
    }
    string_array_deinit(&parts);

    return ret;
}

static bool
is_valid_vertex(struct mesh *mesh, struct vertex *vertex) {
    return vertex->vertex_index >= 0 && vertex->vertex_index < mesh->vertices.len &&
            vertex->normal_index < mesh->normals.len && vertex->texture_index < mesh->textures.len;
}

static bool
mesh_add_face(struct mesh *mesh, string_array_t *parts) {
    if(parts->len < 4) {
        return false;
    }

    struct vertex first = {-1, -1, -1}, last = {-1, -1, -1}, cur;
    for(int i = 1; i < parts->len; i++) {
        if(!parse_face_vertex(&parts->data[i], &cur) || !is_valid_vertex(mesh, &cur)) {
            return false;
        }

        if(first.vertex_index == -1) {
            first = cur;
        } else if(last.vertex_index == -1) {
            last = cur;
        } else {
            face_array_push(&mesh->faces, (struct face){first, last, cur});
            last = cur;
        }
    }

    return true;
}

static bool
are_same_file(string_t *p1, string_t *p2) {
    struct stat s1, s2;
    if(stat(string_c_string_view(p1), &s1) != 0) {
        return false;
    }

    if(stat(string_c_string_view(p2), &s2) != 0) {
        return false;
    }

    return s1.st_ino == s2.st_ino && s1.st_dev == s2.st_dev;
}

static struct texture *
try_find_texture(struct assets_manager *manager, string_t *path) {
    list_for_each(struct texture, iter, &manager->textures, link) {
        if(are_same_file(path, &iter->path)) {
            return iter;
        }
    }

    return NULL;
}

static void
texture_ref(struct texture *texture) {
    texture->ref_count++;
}

static void
texture_unref(struct texture *texture) {
    texture->ref_count--;
    if(texture->ref_count == 0) {
        list_remove(&texture->link);
        string_deinit(&texture->path);
        free(texture->pixels);
        free(texture);
    }
}

static void
material_destroy(struct material *material) {
    string_deinit(&material->name);

    if(material->texture) {
        texture_unref(material->texture);
    }

    free(material);
}

// name contains the path to the file from the current context. it will be expanded to a full path to it. this is useful
// in multiple scenarios, e.g. when we are loading a material found in an .obj file, or a texture found in a .mtl file
static void
create_path_from_current_context(string_t *current, string_t *path) {
    if(path->len == 0 || path->data[0] == '/') {
        // absolute path (this should not really happen, but we got it covered)
        return;
    }

    int last_slash = string_index_of_reverse(current, '/');
    if(last_slash == -1) {
        // means we are in the current working directory so nothing to prepand
        return;
    }

    // first move the path out of the way, last_slash + 1 chars forwards; note: the last + 1 we add so there is no
    // reallocation on the next call to `string_c_string_view()`
    string_reserve(path, path->len + last_slash + 1 + 1);
    memmove(path->data + last_slash + 1, path->data, path->len);
    // and then prepand the thing and update the len to reflect that
    memcpy(path->data, current->data, last_slash + 1);
    path->len += last_slash + 1;
}

static struct texture *
texture_load(string_t *path) {
    int width, height, channels;
    u8 *pixels = stbi_load(string_c_string_view(path), &width, &height, &channels, 4);
    if(!pixels) {
        return NULL;
    }

    struct texture *texture = alloc(sizeof(*texture));
    string_clone(&texture->path, path);
    texture->width = width;
    texture->height = height;
    texture->pixels = pixels;

    return texture;
}

static void
material_add_texture(struct assets_manager *manager, string_t *current_path, struct material *material,
        string_array_t *parts) {
    if(parts->len < 2) {
        return;
    }

    string_t *path = &parts->data[1];
    create_path_from_current_context(current_path, path);
    printf("loading a texture: `%s`\n", string_c_string_view(path));

    struct texture *texture = try_find_texture(manager, path);
    if(texture) {
        material->texture = texture;
        texture_ref(texture);
        return;
    }

    // load the texture and cache it
    texture = texture_load(path);
    if(!texture) {
        printf("no texture\n");
        return;
    }

    material->texture = texture;
    texture_ref(texture);
    list_insert(&manager->textures, &texture->link);
}

static void
mesh_load_materials(struct assets_manager *manager, struct mesh *mesh, string_t *path) {
    struct reader *r = reader_create(string_c_string_view(path));
    if(!r) {
        return;
    }

    struct material *material = NULL;

    string_t line = {0};
    string_array_t parts = {0};
    while(reader_read_line(r, &line)) {
        string_split(&line, ' ', true, &parts);

        string_t *key = &parts.data[0];
        if(string_equal_c_string(key, "newmtl")) {
            if(parts.len < 2) {
                goto err;
            }

            if(material) {
                // finish with the current one
                list_insert(&mesh->materials, &material->link);
            }

            // and create a new one with this name
            material = alloc(sizeof(*material));
            string_clone(&material->name, &parts.data[1]);
        } else if(string_equal_c_string(key, "Kd")) {
            if(parts.len < 4) {
                goto err;
            }

            if(material) {
                material->diffuse_color = (vec3){
                        atof(string_c_string_view(&parts.data[1])),
                        atof(string_c_string_view(&parts.data[2])),
                        atof(string_c_string_view(&parts.data[3])),
                };
            }
        } else if(string_equal_c_string(key, "Ns")) {
            if(parts.len < 2) {
                goto err;
            }

            if(material) {
                material->shininess = atoi(string_c_string_view(&parts.data[1]));
            }
        } else if(string_equal_c_string(key, "Ka")) {
            if(parts.len < 4) {
                goto err;
            }

            if(material) {
                material->ambient_color = (vec3){
                        atof(string_c_string_view(&parts.data[1])),
                        atof(string_c_string_view(&parts.data[2])),
                        atof(string_c_string_view(&parts.data[3])),
                };
            }
        } else if(string_equal_c_string(key, "Ks")) {
            if(parts.len < 4) {
                goto err;
            }

            if(material) {
                material->specular_color = (vec3){
                        atof(string_c_string_view(&parts.data[1])),
                        atof(string_c_string_view(&parts.data[2])),
                        atof(string_c_string_view(&parts.data[3])),
                };
            };
        } else if(string_equal_c_string(key, "d")) {
            if(parts.len < 2) {
                goto err;
            }

            if(material) {
                material->opacity = atof(string_c_string_view(&parts.data[1]));
            }
        } else if(string_equal_c_string(key, "illum")) {
            if(parts.len < 2) {
                goto err;
            }

            if(material) {
                material->illumination_model = atoi(string_c_string_view(&parts.data[1]));
            }
        } else if(string_equal_c_string(key, "map_Kd")) {
            if(material) {
                material_add_texture(manager, path, material, &parts);
            }
        }

        // free the substrings
        for(struct string *iter = parts.data; iter < string_array_end(&parts); iter++) {
            string_deinit(iter);
        }
    }

    if(material) {
        // insert the last one
        list_insert(&mesh->materials, &material->link);
    }

    string_array_deinit(&parts);
    string_deinit(&line);
    reader_destroy(r);

    return;

err:
    for(struct string *iter = parts.data; iter < string_array_end(&parts); iter++) {
        string_deinit(iter);
    }
    string_array_deinit(&parts);
    string_deinit(&line);
    reader_destroy(r);

    if(material) {
        material_destroy(material);
    }
}

static void
mesh_add_material_library(struct assets_manager *manager, struct mesh *mesh, string_array_t *parts) {
    if(parts->len < 2) {
        return;
    }

    for(int i = 1; i < parts->len; i++) {
        string_t *path = &parts->data[1];
        create_path_from_current_context(&mesh->path, path);
        mesh_load_materials(manager, mesh, path);
    }
}

struct mesh *
assets_manager_load_mesh(struct assets_manager *manager, char *path) {
    struct reader *r = reader_create(path);
    if(!r) {
        return NULL;
    }

    struct mesh *mesh = alloc(sizeof(*mesh));
    string_init(&mesh->path, path);
    list_init(&mesh->materials);

    string_t line = {0};
    string_array_t parts = {0};
    while(reader_read_line(r, &line)) {
        string_split(&line, ' ', true, &parts);

        string_t *key = &parts.data[0];
        if(string_equal_c_string(key, "v")) {
            if(!mesh_add_vertex(mesh, &parts)) {
                goto err;
            }
        } else if(string_equal_c_string(key, "vn")) {
            if(!mesh_add_normal(mesh, &parts)) {
                goto err;
            }
        } else if(string_equal_c_string(key, "vt")) {
            if(!mesh_add_texture(mesh, &parts)) {
                goto err;
            }
        } else if(string_equal_c_string(key, "f")) {
            if(!mesh_add_face(mesh, &parts)) {
                goto err;
            }
        } else if(string_equal_c_string(key, "mtllib")) {
            // this is not critical, so we dont fail immediately
            mesh_add_material_library(manager, mesh, &parts);
        }

        // free the substrings
        for(struct string *iter = parts.data; iter < string_array_end(&parts); iter++) {
            string_deinit(iter);
        }
    }

    string_array_deinit(&parts);
    string_deinit(&line);
    reader_destroy(r);

    // insert it into a list, so we can more easily track it
    list_insert(manager->meshes.prev, &mesh->link);

    list_for_each(struct material, iter, &mesh->materials, link) {
        printf("material: %s, has_texture: %d\n", string_c_string_view(&iter->name), iter->texture != NULL);
    }

    return mesh;

err:
    for(struct string *iter = parts.data; iter < string_array_end(&parts); iter++) {
        string_deinit(iter);
    }

    string_array_deinit(&parts);
    string_deinit(&line);
    reader_destroy(r);
    mesh_destroy(mesh);

    return NULL;
}

void
mesh_destroy(struct mesh *mesh) {
    string_deinit(&mesh->path);

    vec3_array_deinit(&mesh->vertices);
    vec3_array_deinit(&mesh->normals);
    vec2_array_deinit(&mesh->textures);
    face_array_deinit(&mesh->faces);

    list_for_each_safe(struct material, iter, &mesh->materials, link) {
        material_destroy(iter);
    }

    if(mesh->link.prev != NULL) {
        list_remove(&mesh->link);
    }

    free(mesh);
}
