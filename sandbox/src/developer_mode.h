#pragma once

#include <spot.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

// The sandbox's developer easter egg. The server unlocks the developer skin and Ray Gun for a player's character when
// authenticated with the developer private key or through the operator console command; presentation follows on
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

    bool IsAuthenticated() const { return m_authenticated; }
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

    static constexpr Spot::Vector3 BackroomsOrigin = {500.0f, 120.0f, 500.0f};
    static Spot::ConsoleResult UseLocalBackrooms(Spot::Scene& scene, std::string_view targetName, float duration);
    // On the server: teleports a player or all living players to the temporary Backrooms maze for duration seconds
    // (or until death when duration is 0), then returns each survivor to the position they were taken from.
    static Spot::ConsoleResult SendToBackrooms(Spot::Scene& scene, std::string_view targetName, float duration);
    static bool IsBackroomsActive(const Spot::Scene& scene);
    static std::optional<float> GetBackroomsTimeLeft(const Spot::Entity& player);
    bool IsInBackrooms() const { return m_inBackrooms; }
    void SetInBackrooms(bool inBackrooms) { m_inBackrooms = inBackrooms; }

    struct MessageBoxRecord {
        std::string title;
        std::string message;
        Spot::MessageBoxIcon icon = Spot::MessageBoxIcon::Information;
    };

    static Spot::ConsoleResult UseLocalMessageBox(Spot::Scene& scene, std::string_view targetName,
                                                  std::string_view title, std::string_view message,
                                                  Spot::MessageBoxIcon icon = Spot::MessageBoxIcon::Information);
    // On the server: sends a modal message box to a player's window, or to every player's window with "all".
    static Spot::ConsoleResult SendMessageBox(Spot::Scene& scene, std::string_view targetName, std::string_view title,
                                              std::string_view message,
                                              Spot::MessageBoxIcon icon = Spot::MessageBoxIcon::Information);
    const std::optional<MessageBoxRecord>& GetLastMessageBox() const { return m_lastMessageBox; }

protected:
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnDisable() override;
    void Serialize(Spot::Archive& archive) override;
    void Replicate(Spot::Archive& archive) override;

private:
    void Authenticate();
    void Apply();
    void Bind();
    void AuthenticateLocal();
    void IssueChallenge(Spot::NetworkObject& object);
    void AnswerChallenge(Spot::NetworkObject& object, const Challenge& challenge) const;
    void VerifyResponse(const Signature& signature);
    void ShowLocalMessageBox(std::string_view title, std::string_view message, Spot::MessageBoxIcon icon);
    std::filesystem::path ResolveKeyPath() const;

    float m_immortalTime = 0.0f;
    Spot::SignalConnection m_blocked;
    bool m_authenticated = false;
    bool m_unlocked = false;
    bool m_inBackrooms = false;
    bool m_skinApplied = false;
    bool m_keyChecked = false;
    bool m_hasChallenge = false;
    Challenge m_challenge{};
    PublicKey m_publicKey = DefaultPublicKey;
    std::filesystem::path m_keyPath;
    std::optional<MessageBoxRecord> m_lastMessageBox;
    Spot::NetworkSystem* m_system = nullptr;
};
