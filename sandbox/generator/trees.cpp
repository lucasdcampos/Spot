#include "trees.h"

#include "texture_painting.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Generator::Trees {

namespace {

using Spot::MeshData;
using Spot::MeshVertex;
using Spot::Vector3;

constexpr Vector3 Up{0.0f, 1.0f, 0.0f};
// The golden angle, in radians, at which leaves and branches spread around a stem so they overlap least.
constexpr float GoldenAngle = 2.39996f;

// Numbers from 0 to 1, the same on every run for the seed.
class Rng {
public:
    explicit Rng(std::uint32_t seed) : m_seed(seed) {}

    float Next() { return Textures::Random(m_count++, 0, m_seed); }
    float Range(float from, float to) { return from + (to - from) * Next(); }
    Vector3 Jitter() { return {Range(-1.0f, 1.0f), Range(-1.0f, 1.0f), Range(-1.0f, 1.0f)}; }

private:
    std::uint32_t m_seed;
    int m_count = 0;
};

Vector3 Perpendicular(Vector3 direction) {
    const Vector3 other = std::abs(direction.y) < 0.9f ? Up : Vector3{1.0f, 0.0f, 0.0f};
    return Spot::Normalize(Spot::Cross(direction, other));
}

// A point of a branch's centerline, and the branch's radius there.
struct Knot {
    Vector3 position;
    float radius;
};

struct Branch {
    std::vector<Knot> knots;
    float length = 0.0f;
};

// Where a branch is, how thick and which way it goes, at a share of the way along it.
struct BranchPoint {
    Vector3 position;
    Vector3 direction;
    float radius;
};

BranchPoint Along(const Branch& branch, float s) {
    const float f = std::clamp(s, 0.0f, 1.0f) * static_cast<float>(branch.knots.size() - 1);
    const std::size_t i = std::min(static_cast<std::size_t>(f), branch.knots.size() - 2);
    const float t = f - static_cast<float>(i);
    const Knot& a = branch.knots[i];
    const Knot& b = branch.knots[i + 1];
    return {a.position + (b.position - a.position) * t, Spot::Normalize(b.position - a.position),
            a.radius + (b.radius - a.radius) * t};
}

// Grows a centerline in steps from the start: each step turns it a little at random by the wobble, and up (or down,
// when negative) by the tropism, as branches reach for light or sag. The radius narrows evenly to the tip's.
Branch Grow(Rng& rng, Vector3 start, Vector3 direction, float length, float radius, float tipRadius, float wobble,
            float tropism, int steps) {
    Branch branch{.knots = {}, .length = length};
    Vector3 position = start;
    for (int i = 0; i <= steps; ++i) {
        const float s = static_cast<float>(i) / static_cast<float>(steps);
        branch.knots.push_back({position, radius + (tipRadius - radius) * s});
        direction = Spot::Normalize(direction + rng.Jitter() * wobble + Up * tropism);
        position = position + direction * (length / static_cast<float>(steps));
    }
    return branch;
}

// A tube around the branch, its rings turned along it so they do not twist. Its bark wraps around it a whole number
// of times, about a meter each, and repeats every meter along it. The flare swells its first meter into buttress
// roots, five of them, as a trunk's foot.
void AddTube(MeshData& mesh, const Branch& branch, int sides, float flare = 0.0f) {
    const std::vector<Knot>& knots = branch.knots;
    const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
    const float around = std::max(1.0f, std::round(2.0f * Spot::Pi * knots[0].radius));
    Vector3 normal = Perpendicular(Spot::Normalize(knots[1].position - knots[0].position));
    float distance = 0.0f;
    for (std::size_t i = 0; i < knots.size(); ++i) {
        const Vector3 previous = knots[i == 0 ? 0 : i - 1].position;
        const Vector3 next = knots[std::min(i + 1, knots.size() - 1)].position;
        const Vector3 tangent = Spot::Normalize(next - previous);
        normal = Spot::Normalize(normal - tangent * Spot::Dot(normal, tangent));
        const Vector3 binormal = Spot::Cross(tangent, normal);
        if (i > 0) {
            distance += Spot::Length(knots[i].position - knots[i - 1].position);
        }
        const float foot = std::max(1.0f - distance, 0.0f);
        for (int j = 0; j <= sides; ++j) {
            const float angle = 2.0f * Spot::Pi * static_cast<float>(j) / static_cast<float>(sides);
            const Vector3 out = normal * std::cos(angle) + binormal * std::sin(angle);
            const float swell = 1.0f + flare * foot * foot * (0.55f + 0.45f * std::cos(angle * 5.0f));
            mesh.vertices.push_back({knots[i].position + out * (knots[i].radius * swell),
                                     out,
                                     {static_cast<float>(j) / static_cast<float>(sides) * around, distance}});
        }
    }
    const auto ring = static_cast<std::uint32_t>(sides + 1);
    for (std::uint32_t i = 0; i + 1 < knots.size(); ++i) {
        for (std::uint32_t j = 0; j < static_cast<std::uint32_t>(sides); ++j) {
            const std::uint32_t a = first + i * ring + j;
            const std::uint32_t c = a + ring;
            mesh.indices.insert(mesh.indices.end(), {a, a + 1, c, a + 1, c + 1, c});
        }
    }
}

// A square card of foliage, as wide as long, from its root along the direction, turned about it to face as near the
// facing as it can; the texture's bottom middle, where its twig starts, is at the root. Its normals are set later.
void AddCard(MeshData& mesh, Vector3 root, Vector3 direction, Vector3 facing, float size) {
    Vector3 width = Spot::Cross(direction, facing);
    if (Spot::Length(width) < 1e-4f) {
        width = Perpendicular(direction);
    }
    width = Spot::Normalize(width) * (size * 0.5f);
    const Vector3 normal = Spot::Normalize(Spot::Cross(width, direction));
    const Vector3 tip = root + direction * size;
    const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({root - width, normal, {0.0f, 1.0f}});
    mesh.vertices.push_back({root + width, normal, {1.0f, 1.0f}});
    mesh.vertices.push_back({tip + width, normal, {1.0f, 0.0f}});
    mesh.vertices.push_back({tip - width, normal, {0.0f, 0.0f}});
    mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2, first, first + 2, first + 3});
}

// Turns the foliage's normals toward the outward one of each point, which the function gives, keeping a little of
// the cards' own: lit as a crown, with a little of its leaves' play.
template <typename Outward>
void ShadeAsCrown(MeshData& foliage, Outward&& outward) {
    for (MeshVertex& vertex : foliage.vertices) {
        const Vector3 out = outward(vertex.position);
        const Vector3 card = Spot::Dot(vertex.normal, out) < 0.0f ? -vertex.normal : vertex.normal;
        vertex.normal = Spot::Normalize(out * 0.75f + card * 0.25f);
    }
}

class OakGrower {
public:
    explicit OakGrower(std::uint32_t seed) : m_rng(seed) {}

    TreeMeshes Grow() {
        const Branch trunk = Trees::Grow(m_rng, {0.0f, -0.4f, 0.0f}, Up, 3.3f, 0.32f, 0.2f, 0.05f, 0.0f, 8);
        AddTube(m_tree.bark, trunk, 14, 0.9f);
        constexpr int Limbs = 5;
        float azimuth = m_rng.Range(0.0f, 2.0f * Spot::Pi);
        for (int limb = 0; limb < Limbs; ++limb) {
            const BranchPoint at = Along(trunk, 0.62f + 0.36f * static_cast<float>(limb) / (Limbs - 1));
            azimuth += GoldenAngle;
            const Vector3 out{std::cos(azimuth), 0.0f, std::sin(azimuth)};
            const float lean = m_rng.Range(0.65f, 1.0f);
            AddBranch(at.position, Spot::Normalize(out * std::sin(lean) + Up * std::cos(lean)), m_rng.Range(2.6f, 3.1f),
                      at.radius * m_rng.Range(0.6f, 0.72f), 1);
        }
        const BranchPoint top = Along(trunk, 1.0f);
        AddBranch(top.position, Spot::Normalize(top.direction + m_rng.Jitter() * 0.15f), 2.6f, top.radius * 0.8f, 1);

        // The crown's center and its half sizes, from its foliage, for its normals.
        Vector3 low{1e9f, 1e9f, 1e9f};
        Vector3 high{-1e9f, -1e9f, -1e9f};
        for (const MeshVertex& vertex : m_tree.foliage.vertices) {
            low = {std::min(low.x, vertex.position.x), std::min(low.y, vertex.position.y),
                   std::min(low.z, vertex.position.z)};
            high = {std::max(high.x, vertex.position.x), std::max(high.y, vertex.position.y),
                    std::max(high.z, vertex.position.z)};
        }
        const Vector3 center = (low + high) * 0.5f;
        const Vector3 extent = (high - low) * 0.5f;
        ShadeAsCrown(m_tree.foliage, [&](Vector3 p) {
            const Vector3 d = p - center;
            return Spot::Normalize(Vector3{d.x / extent.x, d.y / extent.y + 0.25f, d.z / extent.z});
        });
        return std::move(m_tree);
    }

private:
    // Limbs (level 1) split into branches (2) and those into twigs (3); branches and twigs bear the leaves.
    void AddBranch(Vector3 start, Vector3 direction, float length, float radius, int level) {
        constexpr int Steps[] = {0, 7, 5, 3};
        constexpr int Sides[] = {0, 9, 6, 4};
        constexpr float Wobble[] = {0.0f, 0.12f, 0.18f, 0.22f};
        constexpr float Tropism[] = {0.0f, 0.05f, 0.03f, -0.01f};
        const Branch branch = Trees::Grow(m_rng, start, direction, length, radius, radius * 0.35f, Wobble[level],
                                          Tropism[level], Steps[level]);
        AddTube(m_tree.bark, branch, Sides[level]);
        if (level < 3) {
            const int children = level == 1 ? 5 : 4;
            float azimuth = m_rng.Range(0.0f, 2.0f * Spot::Pi);
            for (int child = 0; child < children; ++child) {
                const float s = 0.3f + 0.65f * (static_cast<float>(child) + m_rng.Next() * 0.6f) / children;
                const BranchPoint at = Along(branch, s);
                azimuth += GoldenAngle;
                const Vector3 side = Perpendicular(at.direction);
                const Vector3 out = side * std::cos(azimuth) + Spot::Cross(at.direction, side) * std::sin(azimuth);
                const float spread = m_rng.Range(0.6f, 1.0f);
                AddBranch(at.position,
                          Spot::Normalize(at.direction * std::cos(spread) + out * std::sin(spread) + Up * 0.2f),
                          length * m_rng.Range(0.45f, 0.58f) * (1.15f - s * 0.4f),
                          std::min(at.radius * m_rng.Range(0.55f, 0.7f), radius * 0.6f), level + 1);
            }
        }
        if (level >= 2) {
            const int cards = level == 3 ? 4 : 2;
            for (int card = 0; card < cards; ++card) {
                const float s = level == 3 ? 0.3f + 0.7f * static_cast<float>(card + 1) / cards
                                           : 0.75f + 0.25f * static_cast<float>(card) / cards;
                const BranchPoint at = Along(branch, s);
                const Vector3 out = Spot::Normalize(at.direction + m_rng.Jitter() * 0.6f + Up * 0.15f);
                AddCard(m_tree.foliage, at.position, out, Spot::Normalize(Up + m_rng.Jitter() * 0.8f),
                        m_rng.Range(0.65f, 0.85f));
            }
        }
    }

    Rng m_rng;
    TreeMeshes m_tree;
};

} // namespace

TreeMeshes Oak(std::uint32_t seed) {
    return OakGrower(seed).Grow();
}

TreeMeshes Pine(std::uint32_t seed, bool sparse) {
    Rng rng(seed);
    TreeMeshes tree;
    constexpr float Height = 9.0f;
    const Branch trunk = Grow(rng, {0.0f, -0.4f, 0.0f}, Up, Height + 0.4f, 0.24f, 0.025f, 0.015f, 0.02f, 10);
    AddTube(tree.bark, trunk, sparse ? 8 : 10, 0.45f);
    // Whorls of branches from a little above head height to near the top, each branch the longer the lower it is.
    constexpr float Lowest = 1.7f;
    constexpr float Highest = Height - 0.7f;
    const float spacing = sparse ? 0.5f : 0.4f;
    const int branches = sparse ? 5 : 6;
    float azimuth = rng.Range(0.0f, 2.0f * Spot::Pi);
    for (float y = Lowest; y < Highest; y += spacing * rng.Range(0.85f, 1.15f)) {
        const float t = (y - Lowest) / (Highest - Lowest);
        const BranchPoint at = Along(trunk, (y + 0.4f) / (Height + 0.4f));
        for (int b = 0; b < branches; ++b) {
            azimuth += 2.0f * Spot::Pi / static_cast<float>(branches) + rng.Range(-0.35f, 0.35f);
            const Vector3 out{std::cos(azimuth), 0.0f, std::sin(azimuth)};
            const float length = (0.4f + 2.5f * (1.0f - t)) * rng.Range(0.85f, 1.1f);
            // Branches near the top reach up; lower ones start level and sag.
            const Vector3 direction = Spot::Normalize(out + Up * (0.35f - 0.45f * (1.0f - t)));
            const Branch branch = Grow(rng, at.position, direction, length, 0.018f + 0.028f * (1.0f - t), 0.006f, 0.06f,
                                       -0.06f * (1.0f - t), 3);
            AddTube(tree.bark, branch, sparse ? 4 : 5);
            // Sprays along the branch in crossed pairs, each rolled to a side of it, so they show from any side
            // rather than vanishing edge-on, as flat ones would from the ground.
            const float size = std::min(0.65f + length * 0.3f, 1.3f);
            const int cards = std::max(1, static_cast<int>(length / (size * 0.45f)));
            for (int card = 0; card < cards; ++card) {
                const float s = 0.15f + 0.85f * static_cast<float>(card) / static_cast<float>(cards);
                const BranchPoint on = Along(branch, s);
                const Vector3 along = Spot::Normalize(on.direction + rng.Jitter() * 0.3f);
                const Vector3 side = Spot::Normalize(Spot::Cross(along, Up)) * (rng.Next() < 0.5f ? -1.0f : 1.0f);
                AddCard(tree.foliage, on.position, along, Spot::Normalize(Up + side * 0.7f + rng.Jitter() * 0.3f),
                        size * rng.Range(0.85f, 1.05f));
                AddCard(tree.foliage, on.position, Spot::Normalize(along - Up * 0.25f),
                        Spot::Normalize(Up * 0.5f - side + rng.Jitter() * 0.3f), size * rng.Range(0.7f, 0.9f));
            }
        }
    }
    // The leader: sprays standing around the top.
    const BranchPoint top = Along(trunk, (Highest + 0.4f) / (Height + 0.4f));
    for (int card = 0; card < 3; ++card) {
        const float angle = azimuth + static_cast<float>(card) * 2.0f * Spot::Pi / 3.0f;
        AddCard(tree.foliage, top.position,
                Spot::Normalize(Up + Vector3{std::cos(angle), 0.0f, std::sin(angle)} * 0.2f),
                {std::cos(angle), 0.0f, std::sin(angle)}, 1.1f);
    }
    // Shaded as a cone: out from the trunk and up.
    ShadeAsCrown(tree.foliage, [](Vector3 p) {
        const float radial = std::sqrt(p.x * p.x + p.z * p.z);
        const Vector3 out = radial > 1e-3f ? Vector3{p.x / radial, 0.0f, p.z / radial} : Vector3{};
        return Spot::Normalize(out + Up * 0.45f);
    });
    return tree;
}

} // namespace Generator::Trees
