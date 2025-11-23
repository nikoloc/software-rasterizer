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
    g.camera->pos.y = -1000.0f;

    g.scene = scene_add_tree(NULL);
    g.assets = assets_manager_create();

    char *paths[] = {
            "assets/meshes/mon_ronera.obj",
            "assets/meshes/sofa.obj",
            "assets/meshes/tree.obj",
            "assets/meshes/Grass_Block.obj",
            "assets/meshes/healer.obj",
    };

    for(size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        struct mesh *mesh = assets_manager_load_mesh(g.assets, paths[i]);
        assert(mesh);

        struct scene_mesh *scene_mesh = scene_add_mesh(g.scene, mesh);
        if(i == 0) {
            scene_node_set_scale(&scene_mesh->node, 5.0f);
        } else if(i == 1) {
            scene_node_set_scale(&scene_mesh->node, 100.0f);
        } else if(i == 3) {
            scene_node_set_scale(&scene_mesh->node, 100.0f);
        } else {
            scene_node_set_scale(&scene_mesh->node, 10.0f);
        }
        // meshes usually assume opengl conventions so we flip them
        scene_node_set_rotation(&scene_mesh->node, (vec3){M_PI_2, 0.0f, 0.0f});
        scene_node_set_position(&scene_mesh->node, (vec3){i * 500.0f, 0.0f, 0.0f});
    }

    w_connection_listen(g.conn);

    // we only need to remove the root node, since it will recursively remove its children
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
