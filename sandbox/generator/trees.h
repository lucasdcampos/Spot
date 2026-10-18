#pragma once

#include <spot.h>

#include <cstdint>

// Trees grown from code, standing on their origin with Y up, in meters: branches as tapering tubes that split into
// smaller ones, and foliage as cards cut out of a texture of a twig of leaves or needles (Surface::Leaves and
// Surface::Needles), each card's bottom middle on a branch. The cards' normals point out of the crown rather than off
// the cards, so the foliage is shaded as a whole, lit on the side toward the sun, rather than card by card.
namespace Generator::Trees {

struct TreeMeshes {
    // Its texture goes around each branch a whole number of times and repeats every meter along it.
    Spot::MeshData bark;
    Spot::MeshData foliage;
};

// A broadleaf tree about eight meters tall: a trunk flaring into roots at its foot, five limbs leaning out of it and a
// leader going on up, branching twice more into twigs, its crown about seven meters across.
TreeMeshes Oak(std::uint32_t seed);

// A conifer about nine meters tall: a straight trunk with whorls of branches all the way up, drooping and longer
// toward the ground, each with crossed sprays of needles along it. A sparse one has fewer, coarser branches, for
// forests.
TreeMeshes Pine(std::uint32_t seed, bool sparse);

} // namespace Generator::Trees
