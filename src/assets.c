#include "assets.h"

#include <sys/stat.h>

#include "alloc.h"
#include "macros.h"
#include "reader.h"

struct assets_manager *
assets_manager_create(void) {
    struct assets_manager *manager = alloc(sizeof(*manager));
    list_init(&manager->meshes);
    list_init(&manager->textures);

    return manager;
}

static void
texture_destroy(struct texture *texture) {
    string_deinit(&texture->path);
    free(texture->pixels);
    free(texture);
}

void
assets_manager_destroy(struct assets_manager *manager) {
    list_for_each_safe(struct texture, iter, &manager->textures, link) {
        texture_destroy(iter);
    }

    list_for_each_safe(struct mesh, iter, &manager->meshes, link) {
        mesh_destroy(iter);
    }

    free(manager);
}

static bool
add_vertex(struct mesh *mesh, string_array_t *parts) {
    if(parts->len != 4) {
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
add_normal(struct mesh *mesh, string_array_t *parts) {
    if(parts->len != 4) {
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
add_texture(struct mesh *mesh, string_array_t *parts) {
    if(parts->len != 3) {
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
add_face(struct mesh *mesh, string_array_t *parts) {
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
are_same_file(char *p1, char *p2) {
    struct stat s1, s2;
    if(stat(p1, &s1) != 0) {
        return false;
    }

    if(stat(p2, &s2) != 0) {
        return false;
    }

    return s1.st_ino == s2.st_ino && s1.st_dev == s2.st_dev;
}

static struct texture *
try_find_texture(struct assets_manager *manager, char *path) {
    list_for_each(struct texture, iter, &manager->textures, link) {
        if(are_same_file(path, string_c_string_view(&iter->path))) {
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
    if(material->texture) {
        texture_unref(material->texture);
    }

    string_deinit(&material->path);
    free(material);
}

static struct material *
load_material(struct assets_manager *manager, char *path) {
    // printf("path: %s\n", path);
    struct reader *r = reader_create(path);
    if(!r) {
        return NULL;
    }

    struct material *material = alloc(sizeof(*material));

    string_t line = {0};
    string_array_t parts = {0};
    while(reader_read_line(r, &line)) {
        string_split(&line, ' ', true, &parts);

        string_t *key = &parts.data[0];
        if(string_equal_c_string(key, "Kd")) {
            if(parts.len < 4) {
                goto err;
            }

            material->diffuse_color = (vec3){
                    atof(string_c_string_view(&parts.data[1])),
                    atof(string_c_string_view(&parts.data[2])),
                    atof(string_c_string_view(&parts.data[3])),
            };
        } else if(string_equal_c_string(key, "Ns")) {
            if(parts.len < 2) {
                goto err;
            }

            material->shininess = atoi(string_c_string_view(&parts.data[1]));
        } else if(string_equal_c_string(key, "Ka")) {
            if(parts.len < 4) {
                goto err;
            }

            material->ambient_color = (vec3){
                    atof(string_c_string_view(&parts.data[1])),
                    atof(string_c_string_view(&parts.data[2])),
                    atof(string_c_string_view(&parts.data[3])),
            };
        } else if(string_equal_c_string(key, "Ks")) {
            if(parts.len < 4) {
                goto err;
            }

            material->specular_color = (vec3){
                    atof(string_c_string_view(&parts.data[1])),
                    atof(string_c_string_view(&parts.data[2])),
                    atof(string_c_string_view(&parts.data[3])),
            };
        } else if(string_equal_c_string(key, "d")) {
            if(parts.len < 2) {
                goto err;
            }

            material->opacity = atof(string_c_string_view(&parts.data[1]));
        } else if(string_equal_c_string(key, "illum")) {
            if(parts.len < 2) {
                goto err;
            }

            material->illumination_model = atoi(string_c_string_view(&parts.data[1]));
        } else if(string_equal_c_string(key, "map_Kd")) {
            todo("add texture");
        }

        // free the substrings
        for(struct string *iter = parts.data; iter < string_array_end(&parts); iter++) {
            string_deinit(iter);
        }
    }

    string_array_deinit(&parts);
    string_deinit(&line);
    reader_destroy(r);

    return material;

err:
    for(struct string *iter = parts.data; iter < string_array_end(&parts); iter++) {
        string_deinit(iter);
    }
    string_array_deinit(&parts);
    string_deinit(&line);
    reader_destroy(r);

    material_destroy(material);
    return NULL;
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

static bool
add_material(struct assets_manager *manager, struct mesh *mesh, string_array_t *parts) {
    if(parts->len < 2) {
        return false;
    }

    string_t *path = &parts->data[1];
    for(int i = 0; i < path->len; i++) {
        if(path->data[i] == 'l') {
            printf("%d %d %d\n", i, path->len, path->cap);
            break;
        }
    }

    // printf("path before: `%s`\n", string_c_string_view(path));
    // create_path_from_current_context(&mesh->path, path);
    // printf("path after: `%s`\n", string_c_string_view(path));

    struct material *material = load_material(manager, string_c_string_view(path));
    if(!material) {
        return false;
    }

    list_insert(&mesh->materials, &material->link);
    return true;
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
        printf("%d, `%s`\n", line.len, string_c_string_view(&line));
        string_split(&line, ' ', true, &parts);

        string_t *key = &parts.data[0];
        if(string_equal_c_string(key, "v")) {
            if(!add_vertex(mesh, &parts)) {
                goto err;
            }
        } else if(string_equal_c_string(key, "vn")) {
            if(!add_normal(mesh, &parts)) {
                goto err;
            }
        } else if(string_equal_c_string(key, "vt")) {
            if(!add_texture(mesh, &parts)) {
                goto err;
            }
        } else if(string_equal_c_string(key, "f")) {
            if(!add_face(mesh, &parts)) {
                goto err;
            }
        } else if(string_equal_c_string(key, "mtllib")) {
            printf("`%s`   \n", string_c_string_view(&parts.data[1]));
            if(!add_material(manager, mesh, &parts)) {
                assert(false);
                goto err;
            }
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
