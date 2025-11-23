#ifndef RENDER_H
#define RENDER_H

#include "camera.h"
#include "ints.h"
#include "scene.h"

void
render(struct scene_tree* scene, struct camera* camera, u32* buffer, float* depth_buffer);

#endif
