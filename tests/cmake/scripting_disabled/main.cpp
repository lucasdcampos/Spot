#include <spot.h>

#if defined(SPOT_SCRIPTING) || defined(SPOT_ENABLE_LUAU)
#error Scripting-disabled consumers must not inherit scripting or Luau definitions
#endif

namespace {
class NativeComponent final : public Spot::Component {
protected:
    void OnUpdate(float deltaTime) override { GetTransform().SetPosition({deltaTime, 0, 0}); }
};
} // namespace

int main() {
    Spot::Scene scene;
    auto& native = scene.Spawn<NativeComponent>();
    scene.Update(0.25f);
    return native.GetTransform().GetPosition().x == 0.25f ? 0 : 1;
}
