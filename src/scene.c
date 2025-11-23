#include "scene.h"

#include "alloc.h"
#include "assets.h"
#include "macros.h"

void
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
