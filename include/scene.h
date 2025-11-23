#ifndef SCENE_H
#define SCENE_H

#include "array.h"
#include "vec3.h"

enum scene_node_type {
    SCENE_NODE_TYPE_MESH,
    SCENE_NODE_TYPE_POLYGON,
    SCENE_NODE_TYPE_TREE,
};

struct transform {
    vec3 pos;
    mat3 rot;
    float scale;
};

void
transform_default(struct transform *dest);

struct scene_node {
    // use this fiels and `container_of()` macro to retrive the appropriate structure
    enum scene_node_type type;
    struct scene_tree *parent;

    struct transform transform;
};

struct scene_polygon {
    struct scene_node node;
};

struct scene_mesh {
    struct scene_node node;

    struct mesh *mesh;
};

define_array(struct scene_node *, scene_node_ptr_array);

struct scene_tree {
    struct scene_node node;

    scene_node_ptr_array_t children;
};

struct scene_polygon *
scene_add_polygon(struct scene_tree *parent, int len, vec3 vertices[len]);

struct scene_mesh *
scene_add_mesh(struct scene_tree *parent, struct mesh *mesh);

struct scene_tree *
scene_add_tree(struct scene_tree *parent);

void
scene_node_set_position(struct scene_node *node, vec3 pos);

// rot.x = pitch, rot.y = roll and rot.z = yaw
void
scene_node_set_rotation(struct scene_node *node, vec3 rot);

void
scene_node_set_scale(struct scene_node *node, float scale);

void
scene_node_reparent(struct scene_node *node, struct scene_tree *parent);

void
scene_node_remove(struct scene_node *node);

#endif
