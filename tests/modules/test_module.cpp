// A game module, as projects with code build them, for the tests of loading modules.
#include <spot.h>

namespace {

// Turns its entity around Y, as a component of game code would.
class Spinner final : public Spot::Component {
public:
    float speed = 1.0f;

protected:
    void OnUpdate(float deltaTime) override {
        Spot::Transform& transform = GetEntity().GetTransform();
        transform.SetRotation(Spot::Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, speed * deltaTime) *
                              transform.GetRotation());
    }

    void Serialize(Spot::Archive& archive) override { archive.Field("speed", speed); }
};

class TestGame final : public Spot::Module {
protected:
    void OnStart() override { Spot::RegisterComponent<Spinner>("TestSpinner"); }
};

} // namespace

SPOT_GAME_MODULE(TestGame)
