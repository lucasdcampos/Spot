#include "spot/animation/animation_assets.h"
#include "spot/animation/animation_asset_format.h"
#include "spot/animation/animator_controller.h"
#include "spot/assets/asset_formats.h"
#include "spot/assets/asset_manager.h"

#include <algorithm>
#include <utility>

namespace Spot {
void RegisterAnimationAssets(AssetManager& assets) {
    assets.RegisterLoader(
        AssetType::Skeleton,
        MakeAssetLoader<Skeleton>([](AssetId, std::span<const std::byte> data) { return Detail::DecodeSkeleton(data); },
                                  [](AssetManager&, AssetId, Skeleton& value) {
                                      return std::make_shared<const Skeleton>(std::move(value));
                                  }));
    assets.RegisterLoader(
        AssetType::AnimationClip,
        MakeAssetLoader<AnimationClip>(
            [](AssetId, std::span<const std::byte> data) { return Detail::DecodeAnimationClip(data); },
            [](AssetManager&, AssetId, AnimationClip& value) {
                return std::make_shared<const AnimationClip>(std::move(value));
            }));
    auto controllerLoader = MakeAssetLoader<AnimatorController>(
        [](AssetId, std::span<const std::byte> data) -> std::optional<AnimatorController> {
            const auto text = Detail::DecodeScene(data);
            return text ? AnimatorController::LoadFromString(*text) : std::nullopt;
        },
        [](AssetManager& assets, AssetId, AnimatorController& controller) {
            for (auto& state : controller.states) {
                state.clip = assets.Load<AnimationClip>(state.clip.GetId());
                for (auto& motion : state.blendTree.motions)
                    motion.clip = assets.Load<AnimationClip>(motion.clip.GetId());
            }
            return std::make_shared<const AnimatorController>(std::move(controller));
        });
    controllerLoader.textReferences = true;
    controllerLoader.dependencies = [](const void* data) {
        std::vector<AssetId> ids;
        for (const auto& state : static_cast<const AnimatorController*>(data)->states) {
            ids.push_back(state.clip.GetId());
            for (const auto& motion : state.blendTree.motions)
                ids.push_back(motion.clip.GetId());
        }
        return ids;
    };
    assets.RegisterLoader(AssetType::AnimatorController, std::move(controllerLoader));
}
} // namespace Spot
