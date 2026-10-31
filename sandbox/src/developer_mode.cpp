#include "developer_mode.h"

#include "gameplay/character/character_movement.h"
#include "gameplay/character/character_visual.h"
#include "gameplay/core/player_name.h"
#include "gameplay/core/players.h"
#include "gameplay/effects/effect_library.h"
#include "gameplay/effects/flash.h"
#include "gameplay/health/health.h"
#include "gameplay/menus/settings_menu.h"
#include "gameplay/network/player_info.h"
#include "gameplay/network/remote_controller.h"
#include "gameplay/network/session.h"
#include "gameplay/vehicles/vehicle_seat.h"
#include "gameplay/weapons/firearm.h"
#include <spot/scene/audio/audio_system.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <string_view>

SPOT_COMPONENT(DeveloperMode);

const DeveloperMode::PublicKey DeveloperMode::DefaultPublicKey = {
    0xc1, 0x47, 0xaf, 0x9f, 0x6a, 0xda, 0x97, 0xfb, 0xa0, 0x67, 0xd1, 0xdb, 0x28, 0xbb, 0xbd, 0x6f,
    0x04, 0x9b, 0xb8, 0x4d, 0xd7, 0xe2, 0xa7, 0x54, 0x50, 0x0e, 0x93, 0x84, 0x48, 0x2e, 0x68, 0x78};

namespace {

using FieldElement = std::array<std::int64_t, 16>;
using Point = std::array<FieldElement, 4>;

constexpr FieldElement ZeroField{};
constexpr FieldElement OneField = {1};
constexpr FieldElement CurveD = {0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070,
                                 0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203};
constexpr FieldElement CurveD2 = {0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0,
                                  0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406};
constexpr FieldElement SqrtMinusOne = {0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43,
                                       0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83};
constexpr FieldElement BaseX = {0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c,
                                0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169};
constexpr FieldElement BaseY = {0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666,
                                0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666};
constexpr std::array<std::int64_t, 32> GroupOrderL = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7,
                                                      0xa2, 0xde, 0xf9, 0xde, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10};

class Sha512 {
public:
    void Update(std::span<const std::uint8_t> data) {
        m_totalBytes += data.size();
        for (const std::uint8_t byte : data) {
            m_block[m_blockLength++] = byte;
            if (m_blockLength == m_block.size()) {
                Compress(m_block.data());
                m_blockLength = 0;
            }
        }
    }

    std::array<std::uint8_t, 64> Finalize() {
        m_block[m_blockLength++] = 0x80;
        if (m_blockLength > 112) {
            std::fill(m_block.begin() + static_cast<std::ptrdiff_t>(m_blockLength), m_block.end(), std::uint8_t{0});
            Compress(m_block.data());
            m_blockLength = 0;
        }
        std::fill(m_block.begin() + static_cast<std::ptrdiff_t>(m_blockLength), m_block.begin() + 120, std::uint8_t{0});
        const std::uint64_t bits = m_totalBytes * 8;
        for (std::size_t i = 0; i < 8; ++i) {
            m_block[120 + i] = static_cast<std::uint8_t>(bits >> (56 - i * 8));
        }
        Compress(m_block.data());
        std::array<std::uint8_t, 64> digest{};
        for (std::size_t i = 0; i < 8; ++i) {
            for (std::size_t j = 0; j < 8; ++j) {
                digest[i * 8 + j] = static_cast<std::uint8_t>(m_state[i] >> (56 - j * 8));
            }
        }
        return digest;
    }

private:
    static constexpr std::array<std::uint64_t, 80> K = {
        0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
        0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
        0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
        0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
        0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
        0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
        0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
        0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
        0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
        0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
        0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
        0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
        0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
        0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
        0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
        0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
        0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
        0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
        0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
        0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL};

    void Compress(const std::uint8_t* block) {
        std::array<std::uint64_t, 80> w{};
        for (std::size_t i = 0; i < 16; ++i) {
            std::uint64_t word = 0;
            for (std::size_t j = 0; j < 8; ++j) {
                word = (word << 8) | block[i * 8 + j];
            }
            w[i] = word;
        }
        for (std::size_t i = 16; i < 80; ++i) {
            const std::uint64_t s0 = std::rotr(w[i - 15], 1) ^ std::rotr(w[i - 15], 8) ^ (w[i - 15] >> 7);
            const std::uint64_t s1 = std::rotr(w[i - 2], 19) ^ std::rotr(w[i - 2], 61) ^ (w[i - 2] >> 6);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        auto [a, b, c, d, e, f, g, h] = m_state;
        for (std::size_t i = 0; i < 80; ++i) {
            const std::uint64_t s1 = std::rotr(e, 14) ^ std::rotr(e, 18) ^ std::rotr(e, 41);
            const std::uint64_t ch = (e & f) ^ (~e & g);
            const std::uint64_t temp1 = h + s1 + ch + K[i] + w[i];
            const std::uint64_t s0 = std::rotr(a, 28) ^ std::rotr(a, 34) ^ std::rotr(a, 39);
            const std::uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint64_t temp2 = s0 + maj;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        m_state[0] += a;
        m_state[1] += b;
        m_state[2] += c;
        m_state[3] += d;
        m_state[4] += e;
        m_state[5] += f;
        m_state[6] += g;
        m_state[7] += h;
    }

    std::array<std::uint64_t, 8> m_state = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL,
                                            0xa54ff53a5f1d36f1ULL, 0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
                                            0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};
    std::array<std::uint8_t, 128> m_block{};
    std::uint64_t m_totalBytes = 0;
    std::size_t m_blockLength = 0;
};

void Carry25519(FieldElement& elem) {
    for (std::size_t i = 0; i < 16; ++i) {
        elem[i] += 1LL << 16;
        const std::int64_t carry = elem[i] >> 16;
        elem[(i + 1) * (i < 15)] += carry - 1 + 37 * (carry - 1) * (i == 15);
        elem[i] -= carry << 16;
    }
}

void Select25519(FieldElement& p, FieldElement& q, std::int64_t bit) {
    const std::int64_t mask = ~(bit - 1);
    for (std::size_t i = 0; i < 16; ++i) {
        const std::int64_t diff = mask & (p[i] ^ q[i]);
        p[i] ^= diff;
        q[i] ^= diff;
    }
}

FieldElement Add(const FieldElement& a, const FieldElement& b) {
    FieldElement out{};
    for (std::size_t i = 0; i < 16; ++i) {
        out[i] = a[i] + b[i];
    }
    return out;
}

FieldElement Sub(const FieldElement& a, const FieldElement& b) {
    FieldElement out{};
    for (std::size_t i = 0; i < 16; ++i) {
        out[i] = a[i] - b[i];
    }
    return out;
}

FieldElement Mul(const FieldElement& a, const FieldElement& b) {
    std::array<std::int64_t, 31> product{};
    for (std::size_t i = 0; i < 16; ++i) {
        for (std::size_t j = 0; j < 16; ++j) {
            product[i + j] += a[i] * b[j];
        }
    }
    for (std::size_t i = 0; i < 15; ++i) {
        product[i] += 38 * product[i + 16];
    }
    FieldElement out{};
    for (std::size_t i = 0; i < 16; ++i) {
        out[i] = product[i];
    }
    Carry25519(out);
    Carry25519(out);
    return out;
}

FieldElement Square(const FieldElement& a) {
    return Mul(a, a);
}

FieldElement Invert(const FieldElement& a) {
    FieldElement c = a;
    for (int i = 253; i >= 0; --i) {
        c = Square(c);
        if (i != 2 && i != 4) {
            c = Mul(c, a);
        }
    }
    return c;
}

FieldElement Pow2523(const FieldElement& a) {
    FieldElement c = a;
    for (int i = 250; i >= 0; --i) {
        c = Square(c);
        if (i != 1) {
            c = Mul(c, a);
        }
    }
    return c;
}

std::array<std::uint8_t, 32> Pack25519(const FieldElement& input) {
    FieldElement t = input;
    Carry25519(t);
    Carry25519(t);
    Carry25519(t);
    for (int pass = 0; pass < 2; ++pass) {
        FieldElement m{};
        m[0] = t[0] - 0xffed;
        for (std::size_t i = 1; i < 15; ++i) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        const std::int64_t borrow = (m[15] >> 16) & 1;
        m[14] &= 0xffff;
        Select25519(t, m, 1 - borrow);
    }
    std::array<std::uint8_t, 32> out{};
    for (std::size_t i = 0; i < 16; ++i) {
        out[2 * i] = static_cast<std::uint8_t>(t[i] & 0xff);
        out[2 * i + 1] = static_cast<std::uint8_t>(t[i] >> 8);
    }
    return out;
}

FieldElement Unpack25519(std::span<const std::uint8_t, 32> input) {
    FieldElement out{};
    for (std::size_t i = 0; i < 16; ++i) {
        out[i] = static_cast<std::int64_t>(input[2 * i]) + (static_cast<std::int64_t>(input[2 * i + 1]) << 8);
    }
    out[15] &= 0x7fff;
    return out;
}

std::uint8_t Parity25519(const FieldElement& a) {
    return static_cast<std::uint8_t>(Pack25519(a)[0] & 1);
}

void PointAdd(Point& p, const Point& q) {
    const FieldElement a = Sub(p[1], p[0]);
    const FieldElement t = Sub(q[1], q[0]);
    const FieldElement aMul = Mul(a, t);
    const FieldElement b = Add(p[0], p[1]);
    const FieldElement bMul = Mul(b, Add(q[0], q[1]));
    const FieldElement c = Mul(Mul(p[3], q[3]), CurveD2);
    const FieldElement d = Mul(p[2], Add(q[2], q[2]));
    const FieldElement e = Sub(bMul, aMul);
    const FieldElement f = Sub(d, c);
    const FieldElement g = Add(d, c);
    const FieldElement h = Add(bMul, aMul);
    p[0] = Mul(e, f);
    p[1] = Mul(h, g);
    p[2] = Mul(g, f);
    p[3] = Mul(e, h);
}

void ConditionalSwap(Point& p, Point& q, std::int64_t bit) {
    for (std::size_t i = 0; i < 4; ++i) {
        Select25519(p[i], q[i], bit);
    }
}

Point ScalarMult(const Point& base, const std::uint8_t* scalar) {
    Point p = {ZeroField, OneField, OneField, ZeroField};
    Point q = base;
    for (int i = 255; i >= 0; --i) {
        const std::int64_t bit = (scalar[i >> 3] >> (i & 7)) & 1;
        ConditionalSwap(p, q, bit);
        PointAdd(q, p);
        PointAdd(p, p);
        ConditionalSwap(p, q, bit);
    }
    return p;
}

Point ScalarBase(const std::uint8_t* scalar) {
    const Point base = {BaseX, BaseY, OneField, Mul(BaseX, BaseY)};
    return ScalarMult(base, scalar);
}

std::array<std::uint8_t, 32> PackPoint(const Point& p) {
    const FieldElement zi = Invert(p[2]);
    const FieldElement tx = Mul(p[0], zi);
    const FieldElement ty = Mul(p[1], zi);
    auto out = Pack25519(ty);
    out[31] ^= static_cast<std::uint8_t>(Parity25519(tx) << 7);
    return out;
}

bool UnpackNegatedPoint(Point& r, std::span<const std::uint8_t, 32> packed) {
    const FieldElement y = Unpack25519(packed);
    const FieldElement y2 = Square(y);
    const FieldElement u = Sub(y2, OneField);
    const FieldElement v = Add(Mul(y2, CurveD), OneField);
    const FieldElement v3 = Mul(Square(v), v);
    const FieldElement v7 = Mul(Square(v3), v);
    FieldElement x = Mul(Mul(u, v3), Pow2523(Mul(u, v7)));
    const auto target = Pack25519(u);
    if (Pack25519(Mul(v, Square(x))) != target) {
        x = Mul(x, SqrtMinusOne);
        if (Pack25519(Mul(v, Square(x))) != target) {
            return false;
        }
    }
    const std::uint8_t sign = packed[31] >> 7;
    if (Parity25519(x) != sign) {
        x = Sub(ZeroField, x);
    }
    if (Parity25519(x) != sign) {
        return false;
    }
    r[0] = Sub(ZeroField, x);
    r[1] = y;
    r[2] = OneField;
    r[3] = Mul(r[0], r[1]);
    return true;
}

std::array<std::uint8_t, 32> ReduceModL(std::array<std::int64_t, 64>& x) {
    for (std::size_t i = 63; i >= 32; --i) {
        std::int64_t carry = 0;
        for (std::size_t j = i - 32; j < i - 12; ++j) {
            x[j] += carry - 16 * x[i] * GroupOrderL[j - (i - 32)];
            carry = (x[j] + 128) >> 8;
            x[j] -= carry << 8;
        }
        x[i - 12] += carry;
        x[i] = 0;
    }
    std::int64_t carry = 0;
    for (std::size_t j = 0; j < 32; ++j) {
        x[j] += carry - (x[31] >> 4) * GroupOrderL[j];
        carry = x[j] >> 8;
        x[j] &= 0xff;
    }
    for (std::size_t j = 0; j < 32; ++j) {
        x[j] -= carry * GroupOrderL[j];
    }
    std::array<std::uint8_t, 32> out{};
    for (std::size_t i = 0; i < 32; ++i) {
        x[i + 1] += x[i] >> 8;
        out[i] = static_cast<std::uint8_t>(x[i] & 0xff);
    }
    return out;
}

bool IsCanonicalScalar(std::span<const std::uint8_t, 32> s) {
    for (int i = 31; i >= 0; --i) {
        const auto byte = s[static_cast<std::size_t>(i)];
        const auto limit = static_cast<std::uint8_t>(GroupOrderL[static_cast<std::size_t>(i)]);
        if (byte < limit) {
            return true;
        }
        if (byte > limit) {
            return false;
        }
    }
    return false;
}

std::string ToHex(std::span<const std::uint8_t> bytes) {
    static constexpr char Digits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(bytes.size() * 2);
    for (const std::uint8_t byte : bytes) {
        hex.push_back(Digits[byte >> 4]);
        hex.push_back(Digits[byte & 0x0f]);
    }
    return hex;
}

int HexNibble(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

bool FromHex(std::string_view text, std::span<std::uint8_t> out) {
    while (!text.empty() &&
           (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n')) {
        text.remove_suffix(1);
    }
    if (text.size() != out.size() * 2) {
        return false;
    }
    for (std::size_t i = 0; i < out.size(); ++i) {
        const int hi = HexNibble(text[2 * i]);
        const int lo = HexNibble(text[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    return true;
}

std::optional<DeveloperMode::Seed> LoadSeed(const std::filesystem::path& path) {
    if (path.empty()) {
        return std::nullopt;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    DeveloperMode::Seed seed{};
    if (!FromHex(contents, seed)) {
        return std::nullopt;
    }
    return seed;
}

DeveloperMode::Challenge GenerateChallenge() {
    std::random_device rd;
    DeveloperMode::Challenge challenge{};
    for (std::size_t i = 0; i < challenge.size(); i += sizeof(unsigned int)) {
        const unsigned int value = rd();
        for (std::size_t j = 0; j < sizeof(unsigned int) && i + j < challenge.size(); ++j) {
            challenge[i + j] = static_cast<std::uint8_t>((value >> (j * 8)) & 0xff);
        }
    }
    return challenge;
}

bool MayUseDeveloperCommands(Spot::NetPeerId from) {
    if (const auto* session = Gameplay::Session::Get(); session && session->IsActive()) {
        return session->MayCheat(from);
    }
    return Spot::Console::Get().AreCheatsEnabled();
}

struct GodRequest {
    static constexpr std::string_view NetName = "Sandbox.GodRequest";
    bool toggle = true;
    bool enabled = false;
    void Serialize(Spot::Archive& archive) {
        archive.Field("toggle", toggle);
        archive.Field("enabled", enabled);
    }
};

struct DeveloperRequest {
    static constexpr std::string_view NetName = "Sandbox.DeveloperRequest";
    void Serialize(Spot::Archive&) {}
};

struct DeveloperChallengeRequest {
    static constexpr std::string_view NetName = "Sandbox.DeveloperChallengeRequest";
    void Serialize(Spot::Archive&) {}
};

struct DeveloperChallenge {
    static constexpr std::string_view NetName = "Sandbox.DeveloperChallenge";
    std::string challenge;
    void Serialize(Spot::Archive& archive) { archive.Field("challenge", challenge); }
};

struct DeveloperResponse {
    static constexpr std::string_view NetName = "Sandbox.DeveloperResponse";
    std::string signature;
    void Serialize(Spot::Archive& archive) { archive.Field("signature", signature); }
};

struct DeveloperPowerRequest {
    static constexpr std::string_view NetName = "Sandbox.DeveloperPowerRequest";
    DeveloperMode::Power power = DeveloperMode::Power::Smite;
    std::string playerName;

    void Serialize(Spot::Archive& archive) {
        archive.Field("power", power);
        archive.Field("playerName", playerName);
    }
};

struct SmiteStrike {
    static constexpr std::string_view NetName = "Sandbox.SmiteStrike";
    Spot::Vector3 position;
    void Serialize(Spot::Archive& archive) { archive.Field("position", position); }
};

void ShowSmite(Spot::Scene& scene, Spot::Vector3 position, const Spot::AssetRef<Spot::Sound>& sound = {}) {
    Gameplay::ParticleLayer layer;
    layer.settings.maxParticles = 256;
    layer.settings.speed = {0, 0};
    layer.settings.gravity = {};
    layer.settings.lifetime = {0.25f, 0.35f};
    layer.settings.size = {0.08f, 0.08f};
    layer.settings.startColor = {4, 6, 12, 1};
    layer.settings.blend = Spot::ParticleBlend::Additive;
    auto& pool = Gameplay::EffectPool::Get(scene);
    Spot::Vector3 from = position + Spot::Vector3{0, 24, 0};
    constexpr std::array<Spot::Vector2, 9> Offsets = {{{0.8f, 0.2f},
                                                       {-0.4f, -0.3f},
                                                       {0.6f, 0.1f},
                                                       {-0.9f, 0.4f},
                                                       {0.3f, -0.2f},
                                                       {-0.5f, 0.2f},
                                                       {0.4f, -0.3f},
                                                       {-0.2f, 0.1f},
                                                       {0, 0}}};
    for (std::size_t i = 0; i < Offsets.size(); ++i) {
        const float height = 24.0f * (1.0f - static_cast<float>(i + 1) / static_cast<float>(Offsets.size()));
        const auto to = position + Spot::Vector3{Offsets[i].x, height, Offsets[i].y};
        pool.Spawn(layer, (from + to) * 0.5f, to - from, 1, 192, Spot::Distance(from, to));
        from = to;
    }
    Gameplay::Flash::Spawn(scene, position, {0.6f, 0.8f, 1, 1}, 12, 20, 0.35f, 0.8f);
    if (sound) {
        scene.GetOrAddSystem<Spot::AudioSystem>().PlayOneShot(
            sound.GetShared(), position,
            {.volume = 1.3f,
             .bus = "Effects",
             .spatialSettings = {.minDistance = 8.0f, .maxDistance = 300.0f},
             .priority = 10,
             .maxInstances = 4});
    }
}

DeveloperMode* Sender(Spot::NetworkObject& object, Spot::NetPeerId from) {
    DeveloperMode* found = nullptr;
    object.GetScene().ForEachComponent<Gameplay::RemoteController>([&](auto& controller) {
        auto* body = controller.GetPlayerPawn();
        if (controller.GetPeer() == from && body && &body->GetEntity() == &object.GetEntity()) {
            found = body->GetEntity().GetComponent<DeveloperMode>();
        }
    });
    return found;
}

struct Target {
    Spot::Entity* entity = nullptr;
    std::string error;
};

Target FindTarget(Spot::Scene& scene, std::string_view name) {
    if (!Gameplay::IsValidPlayerName(name)) {
        return {nullptr, "Enter a player nickname without spaces"};
    }
    Target target;
    const auto infos = Gameplay::PlayerInfos::GetAll(scene);
    for (const auto* info : infos) {
        if (info->name != name) {
            continue;
        }
        if (target.entity) {
            return {nullptr, "More than one player has that nickname"};
        }
        target.entity = scene.GetEntity(info->pawn);
    }
    if (infos.empty() && !Spot::Network::IsClient(scene)) {
        const auto* settings = scene.FindComponent<Gameplay::SettingsMenu>();
        const std::string localName = settings ? settings->GetSettings().playerName : "Player";
        if (name == localName) {
            if (auto* pawn = Gameplay::Players::GetBody(scene)) {
                target.entity = &pawn->GetEntity();
            }
        }
    }
    if (!target.entity) {
        target.error = "Player not found: " + std::string(name);
    }
    return target;
}

Gameplay::PlayerInfo* FindInfo(Spot::Entity& character) {
    for (auto* info : Gameplay::PlayerInfos::GetAll(character.GetScene())) {
        if (info->pawn == character.GetId()) {
            return info;
        }
    }
    return nullptr;
}

} // namespace

DeveloperMode::PublicKey DeveloperMode::DerivePublicKey(const Seed& seed) {
    Sha512 hash;
    hash.Update(seed);
    auto digest = hash.Finalize();
    digest[0] &= 248;
    digest[31] &= 127;
    digest[31] |= 64;
    return PackPoint(ScalarBase(digest.data()));
}

DeveloperMode::Signature DeveloperMode::Sign(const Seed& seed, std::span<const std::uint8_t> message) {
    Sha512 seedHash;
    seedHash.Update(seed);
    auto digest = seedHash.Finalize();
    digest[0] &= 248;
    digest[31] &= 127;
    digest[31] |= 64;
    const PublicKey publicKey = PackPoint(ScalarBase(digest.data()));

    Sha512 nonceHash;
    nonceHash.Update(std::span<const std::uint8_t>(digest.data() + 32, 32));
    nonceHash.Update(message);
    const auto nonceDigest = nonceHash.Finalize();
    std::array<std::int64_t, 64> nonceWide{};
    for (std::size_t i = 0; i < 64; ++i) {
        nonceWide[i] = nonceDigest[i];
    }
    const auto rScalar = ReduceModL(nonceWide);
    const auto rPoint = PackPoint(ScalarBase(rScalar.data()));

    Sha512 challengeHash;
    challengeHash.Update(rPoint);
    challengeHash.Update(publicKey);
    challengeHash.Update(message);
    const auto hDigest = challengeHash.Finalize();
    std::array<std::int64_t, 64> hWide{};
    for (std::size_t i = 0; i < 64; ++i) {
        hWide[i] = hDigest[i];
    }
    const auto hScalar = ReduceModL(hWide);

    std::array<std::int64_t, 64> sWide{};
    for (std::size_t i = 0; i < 32; ++i) {
        sWide[i] = rScalar[i];
    }
    for (std::size_t i = 0; i < 32; ++i) {
        for (std::size_t j = 0; j < 32; ++j) {
            sWide[i + j] += static_cast<std::int64_t>(hScalar[i]) * static_cast<std::int64_t>(digest[j]);
        }
    }
    const auto sScalar = ReduceModL(sWide);

    Signature signature{};
    std::copy(rPoint.begin(), rPoint.end(), signature.begin());
    std::copy(sScalar.begin(), sScalar.end(), signature.begin() + 32);
    return signature;
}

bool DeveloperMode::Verify(const PublicKey& publicKey, std::span<const std::uint8_t> message,
                           const Signature& signature) {
    const std::span<const std::uint8_t, 32> rBytes(signature.data(), 32);
    const std::span<const std::uint8_t, 32> sBytes(signature.data() + 32, 32);
    if (!IsCanonicalScalar(sBytes)) {
        return false;
    }
    Point negA{};
    if (!UnpackNegatedPoint(negA, publicKey)) {
        return false;
    }
    Sha512 challengeHash;
    challengeHash.Update(rBytes);
    challengeHash.Update(publicKey);
    challengeHash.Update(message);
    const auto hDigest = challengeHash.Finalize();
    std::array<std::int64_t, 64> hWide{};
    for (std::size_t i = 0; i < 64; ++i) {
        hWide[i] = hDigest[i];
    }
    const auto hScalar = ReduceModL(hWide);

    Point p = ScalarBase(sBytes.data());
    const Point q = ScalarMult(negA, hScalar.data());
    PointAdd(p, q);
    const auto check = PackPoint(p);
    std::uint8_t diff = 0;
    for (std::size_t i = 0; i < 32; ++i) {
        diff |= check[i] ^ rBytes[i];
    }
    return diff == 0;
}

std::filesystem::path DeveloperMode::GetDefaultKeyPath() {
    const auto directory = Spot::GetUserDataDirectory("", "Spot");
    return directory.empty() ? std::filesystem::path{} : directory / KeyFileName;
}

void DeveloperMode::SetKeyPath(std::filesystem::path path) {
    m_keyPath = std::move(path);
    m_keyChecked = false;
}

void DeveloperMode::SetPublicKey(PublicKey publicKey) {
    m_publicKey = publicKey;
    m_keyChecked = false;
}

std::filesystem::path DeveloperMode::ResolveKeyPath() const {
    if (!m_keyPath.empty()) {
        return m_keyPath;
    }
    if (!GetScene().IsLoaded()) {
        return {};
    }
    auto& app = Spot::Application::Get();
    const auto* game = app.GetGameSettings();
    const auto userDir = app.GetUserDataDirectory();
    const auto defaultGameDir =
        game && !game->name.empty() ? Spot::GetUserDataDirectory("Spot Games", game->name) : std::filesystem::path{};
    if (userDir.empty() || userDir == defaultGameDir) {
        return GetDefaultKeyPath();
    }
    return userDir / KeyFileName;
}

void DeveloperMode::Bind() {
    auto* system = Spot::Network::Get(GetScene());
    if (system == m_system) {
        return;
    }
    m_system = system;
    if (system && system->IsClient()) {
        system->OnObjectMessage<SmiteStrike>([](Spot::NetworkObject& object, Spot::NetPeerId, SmiteStrike& strike) {
            const auto* mode = object.GetEntity().GetComponent<DeveloperMode>();
            ShowSmite(object.GetScene(), strike.position, mode ? mode->smiteSound : Spot::AssetRef<Spot::Sound>{});
        });
        system->OnObjectMessage<DeveloperChallenge>(
            [](Spot::NetworkObject& object, Spot::NetPeerId, DeveloperChallenge& message) {
                Challenge challenge{};
                if (!FromHex(message.challenge, challenge)) {
                    return;
                }
                if (auto* mode = object.GetEntity().GetComponent<DeveloperMode>()) {
                    mode->AnswerChallenge(object, challenge);
                }
            });
    }
    if (system && system->IsServer()) {
        system->OnObjectMessage<GodRequest>([](Spot::NetworkObject& object, Spot::NetPeerId from, GodRequest& request) {
            if (!MayUseDeveloperCommands(from)) {
                return;
            }
            if (auto* mode = Sender(object, from)) {
                if (auto* health = mode->GetEntity().GetComponent<Gameplay::Health>()) {
                    health->SetGod(request.toggle ? !health->IsGod() : request.enabled);
                }
            }
        });
        system->OnObjectMessage<DeveloperRequest>(
            [](Spot::NetworkObject& object, Spot::NetPeerId from, DeveloperRequest&) {
                if (!MayUseDeveloperCommands(from)) {
                    Spot::Log::Warning("Developer mode refused: cheats or operator permission are required");
                    return;
                }
                // A request can unlock only the sender's own character, including while driving another pawn.
                if (auto* mode = Sender(object, from)) {
                    mode->Unlock();
                }
            });
        system->OnObjectMessage<DeveloperChallengeRequest>(
            [](Spot::NetworkObject& object, Spot::NetPeerId from, DeveloperChallengeRequest&) {
                if (auto* mode = Sender(object, from)) {
                    mode->IssueChallenge(object);
                }
            });
        system->OnObjectMessage<DeveloperResponse>(
            [](Spot::NetworkObject& object, Spot::NetPeerId from, DeveloperResponse& response) {
                Signature signature{};
                if (!FromHex(response.signature, signature)) {
                    return;
                }
                if (auto* mode = Sender(object, from)) {
                    mode->VerifyResponse(signature);
                }
            });
        system->OnObjectMessage<DeveloperPowerRequest>([](Spot::NetworkObject& object, Spot::NetPeerId from,
                                                          DeveloperPowerRequest& request) {
            if (!MayUseDeveloperCommands(from)) {
                Spot::Log::Warning("Developer power refused: cheats or operator permission are required");
                return;
            }
            if (auto* mode = Sender(object, from)) {
                const auto result = Strike(object.GetScene(), request.power, request.playerName, &mode->GetEntity());
                if (!result) {
                    Spot::Log::Warning("Developer power refused: {}", result.message);
                }
            }
        });
    }
}

void DeveloperMode::AuthenticateLocal() {
    if (m_unlocked || m_keyChecked || Gameplay::Players::FindComponent<DeveloperMode>(GetScene()) != this) {
        return;
    }
    if (Spot::Network::IsClient(GetScene())) {
        auto* object = Spot::Network::GetObject(GetEntity());
        if (!m_system || !object) {
            return;
        }
        m_keyChecked = true;
        if (LoadSeed(ResolveKeyPath())) {
            m_system->SendToServer(*object, DeveloperChallengeRequest{});
        }
        return;
    }
    m_keyChecked = true;
    const auto seed = LoadSeed(ResolveKeyPath());
    if (!seed) {
        return;
    }
    const Challenge challenge = GenerateChallenge();
    const Signature signature = Sign(*seed, challenge);
    if (Verify(m_publicKey, challenge, signature)) {
        Unlock();
    }
}

void DeveloperMode::IssueChallenge(Spot::NetworkObject& object) {
    if (!m_system || !m_system->IsServer() || m_unlocked) {
        return;
    }
    m_challenge = GenerateChallenge();
    m_hasChallenge = true;
    m_system->SendToOwner(object, DeveloperChallenge{ToHex(m_challenge)});
}

void DeveloperMode::AnswerChallenge(Spot::NetworkObject& object, const Challenge& challenge) const {
    if (!m_system || !m_system->IsClient()) {
        return;
    }
    const auto seed = LoadSeed(ResolveKeyPath());
    if (!seed) {
        return;
    }
    const Signature signature = Sign(*seed, challenge);
    m_system->SendToServer(object, DeveloperResponse{ToHex(signature)});
}

void DeveloperMode::VerifyResponse(const Signature& signature) {
    if (!m_hasChallenge || !Spot::Network::IsServer(GetScene())) {
        return;
    }
    m_hasChallenge = false;
    if (Verify(m_publicKey, m_challenge, signature)) {
        Unlock();
    }
}

Spot::ConsoleResult DeveloperMode::SetLocalGod(Spot::Scene& scene, std::optional<bool> enabled) {
    auto* health = Gameplay::Players::FindComponent<Gameplay::Health>(scene);
    if (!health) {
        return {false, "No player health in this scene"};
    }
    if (auto* system = Spot::Network::Get(scene); system && system->IsClient()) {
        auto* object = Spot::Network::GetObject(health->GetEntity());
        if (!object) {
            return {false, "No network player character"};
        }
        system->SendToServer(*object, GodRequest{!enabled.has_value(), enabled.value_or(false)});
        return {true, "God mode requested"};
    }
    health->SetGod(enabled.value_or(!health->IsGod()));
    return {true, health->IsGod() ? "God mode enabled" : "God mode disabled"};
}

bool DeveloperMode::ActivateLocal(Spot::Scene& scene) {
    auto* mode = Gameplay::Players::FindComponent<DeveloperMode>(scene);
    if (!mode) {
        return false;
    }
    if (auto* system = Spot::Network::Get(scene); system && system->IsClient()) {
        auto* object = Spot::Network::GetObject(mode->GetEntity());
        if (!object) {
            return false;
        }
        system->SendToServer(*object, DeveloperRequest{});
    } else {
        mode->Unlock();
    }
    return true;
}

Spot::ConsoleResult DeveloperMode::UseLocalPower(Spot::Scene& scene, Power power, std::string_view playerName) {
    const auto target = FindTarget(scene, playerName);
    if (!target.entity) {
        return {false, target.error};
    }
    if (auto* system = Spot::Network::Get(scene); system && system->IsClient()) {
        auto* mode = Gameplay::Players::FindComponent<DeveloperMode>(scene);
        auto* object = mode ? Spot::Network::GetObject(mode->GetEntity()) : nullptr;
        if (!object) {
            return {false, "No network player character"};
        }
        system->SendToServer(*object, DeveloperPowerRequest{power, std::string(playerName)});
        return {true, "Developer power requested for " + std::string(playerName)};
    }
    const auto* body = Gameplay::Players::GetBody(scene);
    return Strike(scene, power, playerName, body ? &body->GetEntity() : nullptr);
}

Spot::ConsoleResult DeveloperMode::Strike(Spot::Scene& scene, Power power, std::string_view playerName,
                                          const Spot::Entity* caster) {
    if (!Spot::Network::IsServer(scene)) {
        return {false, "Only the server strikes"};
    }
    if (power != Power::Smite && power != Power::Yeet) {
        return {false, "Unknown developer power"};
    }
    const auto target = FindTarget(scene, playerName);
    if (!target.entity) {
        return {false, target.error};
    }
    auto* health = target.entity->GetComponent<Gameplay::Health>();
    if (!health || health->IsDead()) {
        return {false, "The player is not alive"};
    }
    auto* movement = target.entity->GetComponent<Gameplay::CharacterMovement>();
    if (power == Power::Yeet && !movement) {
        return {false, "The player has no character movement"};
    }
    // Launch the character, including when it was driving, rather than the vehicle it temporarily possessed.
    scene.ForEachComponent<Gameplay::VehicleSeat>([&](auto& seat) {
        auto* occupant = seat.GetOccupant();
        if (occupant && &occupant->GetEntity() == target.entity) {
            seat.Exit();
        }
    });
    const auto position = target.entity->GetTransform().GetWorldPosition();
    const Spot::EntityId by = caster ? caster->GetId() : Spot::EntityId::Invalid;
    if (power == Power::Smite) {
        const auto* mode = target.entity->GetComponent<DeveloperMode>();
        ShowSmite(scene, position, mode ? mode->smiteSound : Spot::AssetRef<Spot::Sound>{});
        if (auto* system = Spot::Network::Get(scene)) {
            if (auto* object = Spot::Network::GetObject(*target.entity)) {
                system->SendToClients(*object, SmiteStrike{position});
            }
        }
        Gameplay::Damage::Apply(scene, target.entity->GetId(),
                                {.amount = std::numeric_limits<float>::max(),
                                 .type = "Smite",
                                 .point = position,
                                 .impulse = {0, -200, 0},
                                 .instigator = by,
                                 .source = by});
    } else {
        // Away from whoever casts it; without one, as from a server's console, back from where the target looks.
        Spot::Vector3 direction = caster ? position - caster->GetTransform().GetWorldPosition() : Spot::Vector3{};
        direction.y = 0;
        if (Spot::LengthSquared(direction) < 0.01f) {
            const auto* pawn = Gameplay::Pawn::Of(caster ? *caster : *target.entity);
            direction = pawn ? pawn->GetViewPoint().direction * (caster ? 1.0f : -1.0f) : Spot::Vector3{0, 0, -1};
            direction.y = 0;
        }
        if (Spot::LengthSquared(direction) < 0.01f) {
            direction = {0, 0, -1};
        }
        movement->Push(Spot::Normalize(direction) * 160.0f + Spot::Vector3{0, 120, 0});
    }
    return {true, std::string(power == Power::Smite ? "Smote " : "Yeeted ") + std::string(playerName)};
}

void DeveloperMode::Unlock() {
    if (!Spot::Network::IsServer(GetScene())) {
        return;
    }
    m_unlocked = true;
    if (auto* info = FindInfo(GetEntity())) {
        info->developer = true;
    }
    Apply();
}

void DeveloperMode::Apply() {
    if (!m_unlocked) {
        return;
    }
    if (auto* effects = Gameplay::EffectLibrary::Find(GetScene()); effects && !effects->Get("ray_gun.tracer")) {
        for (const auto* source : {"muzzle", "tracer"}) {
            if (const auto* original = effects->Get(source)) {
                auto effect = *original;
                effect.name = std::string("ray_gun.") + source;
                effect.light.color = {0.15f, 1.0f, 0.04f, 1};
                for (auto& layer : effect.layers) {
                    layer.settings.startColor = {0.6f, 8.0f, 0.15f, 1};
                }
                effects->effects.push_back(std::move(effect));
            }
        }
    }
    if (auto* weapon = GetScene().GetEntity(rayGun); weapon && !weapon->IsActiveSelf()) {
        weapon->SetActive(true);
        if (auto* firearm = weapon->GetComponent<Gameplay::Firearm>();
            firearm && Spot::Network::IsServer(GetScene()) && firearm->GetAmmo() < 0) {
            firearm->Refill();
        }
    }
    const auto* visual = GetEntity().GetComponent<Gameplay::CharacterVisual>();
    auto* body = visual ? GetScene().GetEntity(visual->modelRoot) : nullptr;
    if (m_skinApplied || !body || !skin) {
        return;
    }
    for (const auto& part : skin->GetParts()) {
        auto* child = body->FindChild(part.name);
        if (!child) {
            child = &body->CreateChild(part.name);
        }
        child->SetLayer(body->GetLayer());
        auto* renderer = child->GetComponent<Spot::SkinnedMeshRenderer>();
        if (!renderer) {
            renderer = &child->AddComponent<Spot::SkinnedMeshRenderer>();
        }
        renderer->SetMesh(part.mesh);
        renderer->SetMaterial(part.material);
        renderer->SetSubmesh(part.submesh);
        renderer->SetAnimator(body->GetId());
        renderer->SetMeshModel(part.transform);
    }
    m_skinApplied = true;
}

void DeveloperMode::OnStart() {
    if (auto* health = GetEntity().GetComponent<Gameplay::Health>()) {
        m_blocked = health->OnDamageBlocked.Connect([this](const Gameplay::DamageInfo& damage) {
            if (!m_unlocked || damage.instigator == GetEntity().GetId()) {
                return;
            }
            auto* attacker = GetScene().GetEntity(damage.instigator);
            auto* pawn = attacker ? Gameplay::Pawn::Of(*attacker) : nullptr;
            if (pawn && FindInfo(pawn->GetEntity())) {
                m_immortalTime = 3.0f;
            }
        });
    }
}

void DeveloperMode::OnUpdate(float deltaTime) {
    m_immortalTime = std::max(0.0f, m_immortalTime - deltaTime);
    if (const auto* health = GetEntity().GetComponent<Gameplay::Health>(); !health || !health->IsGod()) {
        m_immortalTime = 0.0f;
    }
    Bind();
    AuthenticateLocal();
    if (Spot::Network::IsServer(GetScene())) {
        if (auto* info = FindInfo(GetEntity())) {
            if (info->developer || m_unlocked) {
                m_unlocked = true;
                info->developer = true;
            }
        }
    }
    Apply();
}

void DeveloperMode::Replicate(Spot::Archive& archive) {
    archive.Field("unlocked", m_unlocked);
    archive.Field("immortalTime", m_immortalTime);
    if (archive.IsLoading()) {
        Apply();
    }
}

void DeveloperMode::Serialize(Spot::Archive& archive) {
    archive.Field("rayGun", rayGun);
    archive.Field("skin", skin);
    archive.Field("smiteSound", smiteSound);
    archive.Field("unlocked", m_unlocked, {.hidden = true});
}
