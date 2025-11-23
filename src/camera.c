#include "camera.h"

#include "alloc.h"
#include "macros.h"

static void
camera_compute_normals(struct camera *camera) {
    camera->normal = (vec3){
            cosf(camera->pitch) * sinf(camera->yaw),
            cosf(camera->pitch) * cosf(camera->yaw),
            sinf(camera->pitch),
    };

    camera->right = vec3_normalize(vec3_cross(camera->normal, (vec3){0.0f, 0.0f, 1.0f}));
    camera->up = vec3_cross(camera->right, camera->normal);
}

struct camera *
camera_create(float fov, float sensitivity, float speed) {
    struct camera *camera = alloc(sizeof(*camera));
    camera->fov = fov;
    camera->sensitivity = sensitivity;
    camera->speed = speed;

    // set inital normals. note: default is looking along the positive y-axis
    camera_compute_normals(camera);
    return camera;
}

void
camera_destroy(struct camera *camera) {
    free(camera);
}

void
camera_update_viewport(struct camera *camera, int width, int height) {
    camera->width = width;
    camera->height = height;
}

void
camera_update_position(struct camera *camera, struct keys *pressed, float dt) {
    vec3 vel = {0};
    if(pressed->w) {
        vel = vec3_add(vel, camera->normal);
    }
    if(pressed->s) {
        vel = vec3_sub(vel, camera->normal);
    }
    if(pressed->a) {
        vel = vec3_sub(vel, camera->right);
    }
    if(pressed->d) {
        vel = vec3_add(vel, camera->right);
    }

    camera->pos = vec3_add(camera->pos, vec3_scale(dt * camera->speed, vec3_normalize(vel)));
}

void
camera_update_orientation(struct camera *camera, float dx, float dy) {
    dx *= camera->sensitivity;
    dy *= camera->sensitivity;

    camera->yaw += dx;

    camera->pitch -= dy;
    camera->pitch = clamp(camera->pitch, -M_PI_2, +M_PI_2);

    // and compute the new normal vectors
    camera_compute_normals(camera);
}
