#include <assert.h>
#include <w_connection.h>
#include <w_desktop_shell.h>
#include <w_keyboard.h>
#include <w_pointer.h>

#include "assets.h"
#include "camera.h"
#include "scene.h"
#include "state.h"
#include "window.h"

int
main(void) {
    struct state g = {0};

    g.conn = w_connection_create(NULL);
    assert(g.conn);

    g.window = window_create(&g);

    g.camera = camera_create(M_PI_2, 1.0f / 4096.0f, 4.0f);
    g.camera->pos.y = -200.0f;

    g.scene = scene_add_tree(NULL);
    g.assets = assets_manager_create();

    struct mesh *mesh = assets_manager_load_mesh(g.assets, "assets/meshes/tree.obj");
    assert(mesh);

    struct scene_mesh *scene_mesh = scene_add_mesh(g.scene, mesh);
    scene_node_set_scale(&scene_mesh->node, 100.0f);
    scene_node_set_rotation(&scene_mesh->node, (vec3){M_PI_2, 0.0f, 0.0f});

    w_connection_listen(g.conn);

    // we only need to remove the root node, since it will resursively remove its children
    scene_node_remove(&g.scene->node);
    assets_manager_destroy(g.assets);
    camera_destroy(g.camera);
    window_destroy(g.window);
    w_connection_destroy(g.conn);
    if(g.depth_buffer) {
        free(g.depth_buffer);
    }

    return 0;
}
