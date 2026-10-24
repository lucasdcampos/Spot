#pragma once

#include "spot/core/math/vector.h"

namespace Spot {

class Renderer2D;

class UiDrawListener {
public:
    virtual ~UiDrawListener() = default;
    virtual void OnDrawUi(Renderer2D&, Vector2) {}
};

} // namespace Spot
