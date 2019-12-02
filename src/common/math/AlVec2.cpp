/*
* Copyright (c) 2018-present, aliminabc@gmail.com.
*
* This source code is licensed under the MIT license found in the
* LICENSE file in the root directory of this source tree.
*/

#include "AlVec2.h"

void AlVec2::swap(AlVec2 *vec1, AlVec2 *vec2) {
    float tmp = vec1->x;
    vec1->x = vec2->x;
    vec2->x = tmp;

    tmp = vec1->y;
    vec1->y = vec2->y;
    vec2->y = tmp;
}

AlVec2::AlVec2(float x, float y) : Object(), x(x), y(y) {

}

AlVec2::AlVec2(const AlVec2 &o) : Object(), x(o.x), y(o.y) {
}

AlVec2::~AlVec2() {
    x = 0.0f;
    y = 0.0f;
}