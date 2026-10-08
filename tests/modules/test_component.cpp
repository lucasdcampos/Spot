// A component of the test module that registers itself, as components made from the editor do.
#include <spot.h>

namespace {

// Moves its entity along X.
class TestPusher final : public Spot::Component {
public:
    float speed = 1.0f;

protected:
    void OnUpdate(float deltaTime) override {
        Spot::Transform& transform = GetEntity().GetTransform();
        transform.SetPosition(transform.GetPosition() + Spot::Vector3{speed * deltaTime, 0.0f, 0.0f});
    }

    void Serialize(Spot::Archive& archive) override { archive.Field("speed", speed); }
};

} // namespace

SPOT_COMPONENT(TestPusher);
