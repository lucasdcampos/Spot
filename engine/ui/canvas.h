#pragma once

#include "scene/component.h"
#include "ui/ui_layout.h"

namespace Spot {

class Canvas final : public Component {
public:
    UiCanvasSettings settings;

protected:
    void Serialize(Archive& archive) override { settings.Serialize(archive); }
};

class UiRect final : public Component {
public:
    UiRectLayout layout;

protected:
    void Serialize(Archive& archive) override { layout.Serialize(archive); }
};

} // namespace Spot
