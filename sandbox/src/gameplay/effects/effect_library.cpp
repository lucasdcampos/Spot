#include "gameplay/effects/effect_library.h"
#include <spot/scene/audio/audio_system.h>

#include "gameplay/effects/flash.h"

#include <algorithm>
#include <cmath>

namespace Gameplay {

SPOT_COMPONENT(EffectLibrary);

namespace {

// The client that played what plays now itself (see EffectLibrary::Predicted).
Spot::NetPeerId s_predictedBy = Spot::NetPeerId::Invalid;

// An effect a server played, for its clients to play as well.
struct PlayedEffect {
    static constexpr std::string_view NetName = "Gameplay.Effect";
    enum class Kind { AtPoint, Along, To };

    Kind kind = Kind::AtPoint;
    std::string name;
    Spot::Vector3 from;
    Spot::Vector3 to;
    float scale = 1.0f;
    float amount = 1.0f;
    float volume = 1.0f;
    std::uint32_t count = 0;

    void Serialize(Spot::Archive& archive) {
        archive.Field("kind", kind);
        archive.Field("name", name);
        archive.Field("from", from);
        archive.Field("to", to);
        archive.Field("scale", scale);
        archive.Field("amount", amount);
        archive.Field("volume", volume);
        archive.Field("count", count);
    }
};

// A sound a server played, for its clients to play as well.
struct PlayedSound {
    static constexpr std::string_view NetName = "Gameplay.Sound";
    Spot::AssetId sound;
    Spot::Vector3 position;
    float volume = 1.0f;
    float pitch = 1.0f;
    std::string bus;
    float minDistance = 1.0f;
    float maxDistance = 50.0f;
    int priority = 0;
    int maxInstances = 0;

    void Serialize(Spot::Archive& archive) {
        archive.Field("sound", sound);
        archive.Field("position", position);
        archive.Field("volume", volume);
        archive.Field("pitch", pitch);
        archive.Field("bus", bus);
        archive.Field("minDistance", minDistance);
        archive.Field("maxDistance", maxDistance);
        archive.Field("priority", priority);
        archive.Field("maxInstances", maxInstances);
    }
};

template <Spot::NetMessage T>
void Share(Spot::Scene& scene, const T& message) {
    Spot::NetworkSystem* system = Spot::Network::Get(scene);
    if (!system || !system->IsServer()) {
        return;
    }
    for (const Spot::NetPeerId peer : system->GetReadyPeers()) {
        // As it happens or not at all: a late one only confuses.
        if (peer != s_predictedBy) {
            system->GetSession()->Send(peer, message, Spot::NetDelivery::Unreliable);
        }
    }
}

} // namespace

void EffectLibrary::OnUpdate(float /*deltaTime*/) {
    const Spot::NetworkSystem* system = Spot::Network::Get(GetScene());
    Spot::NetworkSession* session = system && system->IsClient() ? system->GetSession() : nullptr;
    if (!session || session == m_session) {
        return;
    }
    m_session = session;
    session->GetMessages().On<PlayedSound>([session](Spot::NetPeerId, PlayedSound& played) {
        Spot::NetworkSystem* bound = session->GetSystem();
        Spot::AssetManager* assets = Spot::Assets::GetManager();
        if (!bound || !assets || !played.sound.IsValid()) {
            return;
        }
        const Spot::AssetRef<Spot::Sound> sound = assets->Load<Spot::Sound>(played.sound);
        if (sound) {
            bound->GetScene().GetOrAddSystem<Spot::AudioSystem>().PlayOneShot(
                sound.GetShared(), played.position,
                {.volume = played.volume,
                 .pitch = played.pitch,
                 .bus = played.bus,
                 .spatialSettings = {.minDistance = played.minDistance, .maxDistance = played.maxDistance},
                 .priority = played.priority,
                 .maxInstances = played.maxInstances});
        }
    });
    session->GetMessages().On<PlayedEffect>([session](Spot::NetPeerId, PlayedEffect& effect) {
        Spot::NetworkSystem* bound = session->GetSystem();
        if (!bound) {
            return;
        }
        Spot::Scene& scene = bound->GetScene();
        switch (effect.kind) {
        case PlayedEffect::Kind::AtPoint:
            Play(scene, effect.name, effect.from, effect.to,
                 {.scale = effect.scale, .amount = effect.amount, .volume = effect.volume});
            break;
        case PlayedEffect::Kind::Along:
            PlayAlong(scene, effect.name, effect.from, effect.to, std::min(effect.count, 1000u));
            break;
        case PlayedEffect::Kind::To:
            PlayTo(scene, effect.name, effect.from, effect.to);
            break;
        }
    });
}

void EffectLight::Serialize(Spot::Archive& archive) {
    archive.Field("color", color, {.tooltip = "Black for no light"});
    archive.Field("range", range, {.min = 0.0f, .max = 1000.0f, .speed = 0.1f, .tooltip = "Meters, times the scale"});
    archive.Field("intensity", intensity, {.min = 0.0f, .max = 1000.0f, .speed = 0.1f});
    archive.Field("lifetime", lifetime, {.min = 0.0f, .max = 10.0f, .speed = 0.01f, .tooltip = "Seconds"});
    archive.Field("flicker", flicker, {.min = 0.0f, .max = 1.0f, .speed = 0.01f});
    archive.Field("offset", offset,
                  {.min = -10.0f, .max = 10.0f, .speed = 0.005f, .tooltip = "Meters along the effect's direction"});
}

void EffectSound::Serialize(Spot::Archive& archive) {
    archive.Field("sound", sound);
    archive.Field("volume", volume, {.min = 0.0f, .max = 4.0f, .speed = 0.01f});
    archive.Field("bus", bus);
    archive.Field("minDistance", minDistance, {.min = 0.0f, .max = 1000.0f, .speed = 0.1f});
    archive.Field("maxDistance", maxDistance, {.min = 0.0f, .max = 10000.0f, .speed = 0.1f});
    archive.Field("priority", priority, {.min = 0.0f, .max = 100.0f, .tooltip = "Higher keeps playing"});
    archive.Field("maxInstances", maxInstances, {.min = 1.0f, .max = 64.0f});
}

void EffectDefinition::Serialize(Spot::Archive& archive) {
    archive.Field("name", name);
    archive.Field("layers", layers);
    archive.Field("light", light);
    archive.Field("sound", sound);
}

EffectLibrary* EffectLibrary::Find(Spot::Scene& scene) {
    EffectLibrary* found = nullptr;
    scene.ForEachComponent<EffectLibrary>([&](EffectLibrary& library) {
        if (!found) {
            found = &library;
        }
    });
    return found;
}

const EffectDefinition* EffectLibrary::Find(Spot::Scene& scene, std::string_view name) {
    const EffectLibrary* library = Find(scene);
    return library ? library->Get(name) : nullptr;
}

const EffectDefinition* EffectLibrary::Get(std::string_view name) const {
    const auto found = std::ranges::find(effects, name, &EffectDefinition::name);
    return found != effects.end() ? &*found : nullptr;
}

void EffectLibrary::Play(Spot::Scene& scene, std::string_view name, Spot::Vector3 position, Spot::Vector3 direction,
                         const EffectOptions& options) {
    const EffectDefinition* effect = Find(scene, name);
    if (!effect || !std::isfinite(options.scale) || options.scale <= 0.0f) {
        return;
    }
    Share<PlayedEffect>(scene, {.name = std::string(name),
                                .from = position,
                                .to = direction,
                                .scale = options.scale,
                                .amount = options.amount,
                                .volume = options.volume});
    const Spot::Vector3 forward =
        Spot::LengthSquared(direction) > 1e-8f ? Spot::Normalize(direction) : Spot::Vector3{0.0f, 1.0f, 0.0f};
    const float scale = std::clamp(options.scale, 0.1f, 4.0f);
    EffectPool& pool = EffectPool::Get(scene);
    for (const ParticleLayer& layer : effect->layers) {
        const auto count =
            std::max(1u, static_cast<std::uint32_t>(static_cast<float>(layer.count) * std::max(options.amount, 0.0f)));
        pool.Spawn(layer, position + forward * layer.offset, forward, scale * layer.scale, count);
    }
    const EffectLight& light = effect->light;
    if (light.color.r + light.color.g + light.color.b > 0.0f && light.intensity > 0.0f && light.lifetime > 0.0f) {
        Flash::Spawn(scene, position + forward * light.offset, light.color, light.range * options.scale,
                     light.intensity, light.lifetime, light.flicker);
    }
    const EffectSound& sound = effect->sound;
    if (sound.sound && options.volume > 0.0f) {
        scene.GetOrAddSystem<Spot::AudioSystem>().PlayOneShot(
            sound.sound.GetShared(), position,
            {.volume = sound.volume * options.volume,
             .bus = sound.bus,
             .spatialSettings = {.minDistance = sound.minDistance, .maxDistance = sound.maxDistance},
             .priority = sound.priority,
             .maxInstances = std::max(sound.maxInstances, 1)});
    }
}

void EffectLibrary::PlayAlong(Spot::Scene& scene, std::string_view name, Spot::Vector3 from, Spot::Vector3 to,
                              std::uint32_t count) {
    const EffectDefinition* effect = Find(scene, name);
    if (!effect || count == 0) {
        return;
    }
    Share<PlayedEffect>(
        scene, {.kind = PlayedEffect::Kind::Along, .name = std::string(name), .from = from, .to = to, .count = count});
    EffectPool& pool = EffectPool::Get(scene);
    for (const ParticleLayer& layer : effect->layers) {
        pool.Spawn(layer, (from + to) * 0.5f, to - from, layer.scale, count, Spot::Distance(from, to));
    }
}

void EffectLibrary::PlayTo(Spot::Scene& scene, std::string_view name, Spot::Vector3 from, Spot::Vector3 to) {
    const EffectDefinition* effect = Find(scene, name);
    const float distance = Spot::Distance(from, to);
    // Too close to see it fly.
    if (!effect || distance < 1.5f) {
        return;
    }
    Share<PlayedEffect>(scene, {.kind = PlayedEffect::Kind::To, .name = std::string(name), .from = from, .to = to});
    EffectPool& pool = EffectPool::Get(scene);
    for (ParticleLayer layer : effect->layers) {
        const float speed = std::max(layer.settings.speed.max, 1.0f);
        // It flies from where it starts until it reaches the far end, less its streak, which would show past it.
        const float travel = std::max(distance - speed * layer.settings.velocityStretch * 0.5f, 0.0f);
        layer.settings.lifetime = {travel / speed, travel / speed};
        layer.settings.coneAngle = 0.0f;
        pool.Spawn(layer, from, to - from, layer.scale);
    }
}

void EffectLibrary::PlaySound(Spot::Scene& scene, const Spot::AssetRef<Spot::Sound>& sound, Spot::Vector3 position,
                              const Spot::OneShotParams& params) {
    if (!sound) {
        return;
    }
    scene.GetOrAddSystem<Spot::AudioSystem>().PlayOneShot(sound.GetShared(), position, params);
    if (sound.GetId().IsValid()) {
        Share(scene, PlayedSound{.sound = sound.GetId(),
                                 .position = position,
                                 .volume = params.volume,
                                 .pitch = params.pitch,
                                 .bus = std::string(params.bus),
                                 .minDistance = params.spatialSettings.minDistance,
                                 .maxDistance = params.spatialSettings.maxDistance,
                                 .priority = params.priority,
                                 .maxInstances = params.maxInstances});
    }
}

EffectLibrary::Predicted::Predicted(const Spot::Entity& entity) : m_previous(s_predictedBy) {
    const Spot::NetworkObject* object = Spot::Network::GetObject(entity);
    if (object && object->IsPredicted() && Spot::Network::IsServer(entity.GetScene())) {
        s_predictedBy = object->GetOwner();
    }
}

EffectLibrary::Predicted::~Predicted() {
    s_predictedBy = m_previous;
}

void EffectLibrary::Serialize(Spot::Archive& archive) {
    archive.Field("effects", effects);
}

} // namespace Gameplay
