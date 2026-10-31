#pragma once

#include <spot.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

// The sandbox's developer easter egg. The server unlocks the developer skin and Ray Gun for a player's character when
// authenticated with the developer private key or through the cheat/operator console command; presentation follows on
// every machine, while PlayerInfo keeps the unlock when the player respawns.
class DeveloperMode final : public Spot::Component, public Spot::Replicated {
public:
    enum class Power { Smite, Yeet };
    using Seed = std::array<std::uint8_t, 32>;
    using PublicKey = std::array<std::uint8_t, 32>;
    using Challenge = std::array<std::uint8_t, 32>;
    using Signature = std::array<std::uint8_t, 64>;

    static constexpr std::string_view KeyFileName = "developer.key";
    static const PublicKey DefaultPublicKey;

    Spot::EntityId rayGun = Spot::EntityId::Invalid;
    Spot::AssetRef<Spot::Model> skin;
    Spot::AssetRef<Spot::Sound> smiteSound;

    static Spot::ConsoleResult SetLocalGod(Spot::Scene& scene, std::optional<bool> enabled);
    float GetImmortalTime() const { return m_immortalTime; }

    bool IsUnlocked() const { return m_unlocked; }
    void Unlock();
    void SetKeyPath(std::filesystem::path path);
    void SetPublicKey(PublicKey publicKey);
    static std::filesystem::path GetDefaultKeyPath();

    static PublicKey DerivePublicKey(const Seed& seed);
    static Signature Sign(const Seed& seed, std::span<const std::uint8_t> message);
    static bool Verify(const PublicKey& publicKey, std::span<const std::uint8_t> message, const Signature& signature);

    static bool ActivateLocal(Spot::Scene& scene);
    static Spot::ConsoleResult UseLocalPower(Spot::Scene& scene, Power power, std::string_view playerName);
    // On the server: strikes the player with the power, for whoever may, as an operator or when cheats are enabled;
    // the caster, if any, is the instigator, and what a yeet throws the target away from.
    static Spot::ConsoleResult Strike(Spot::Scene& scene, Power power, std::string_view playerName,
                                      const Spot::Entity* caster = nullptr);

protected:
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void Serialize(Spot::Archive& archive) override;
    void Replicate(Spot::Archive& archive) override;

private:
    void Apply();
    void Bind();
    void AuthenticateLocal();
    void IssueChallenge(Spot::NetworkObject& object);
    void AnswerChallenge(Spot::NetworkObject& object, const Challenge& challenge) const;
    void VerifyResponse(const Signature& signature);
    std::filesystem::path ResolveKeyPath() const;

    float m_immortalTime = 0.0f;
    Spot::SignalConnection m_blocked;
    bool m_unlocked = false;
    bool m_skinApplied = false;
    bool m_keyChecked = false;
    bool m_hasChallenge = false;
    Challenge m_challenge{};
    PublicKey m_publicKey = DefaultPublicKey;
    std::filesystem::path m_keyPath;
    Spot::NetworkSystem* m_system = nullptr;
};
