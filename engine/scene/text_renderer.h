#pragma once

#include "assets/asset_ref.h"
#include "graphics/color.h"
#include "math/matrix.h"
#include "scene/component.h"

#include <string>
#include <utility>

namespace Spot {

class Font;
struct Camera3D;

// Text starts at the entity's origin, extending right and down in its local XY plane.
class TextRenderer final : public Component {
public:
    const AssetRef<Font>& GetFont() const { return m_font; }
    void SetFont(AssetRef<Font> font) { m_font = std::move(font); }
    const std::string& GetText() const { return m_text; }
    void SetText(std::string text) { m_text = std::move(text); }
    Color GetColor() const { return m_color; }
    void SetColor(Color color) { m_color = color; }
    float GetPixelScale() const { return m_pixelScale; }
    void SetPixelScale(float scale);
    bool IsFacingCamera() const { return m_faceCamera; }
    void SetFacingCamera(bool facing) { m_faceCamera = facing; }
    int GetOrder() const { return m_order; }
    void SetOrder(int order) { m_order = order; }
    Matrix4 GetDrawMatrix(const Camera3D& camera) const;

protected:
    void Serialize(Archive& archive) override;

private:
    AssetRef<Font> m_font;
    std::string m_text = "Text";
    Color m_color = Color::White;
    float m_pixelScale = 0.01f;
    bool m_faceCamera = false;
    int m_order = 0;
};

} // namespace Spot
