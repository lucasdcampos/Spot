#include "spot/animation/animator_controller.h"
#include "spot/assets/asset_formats.h"
#include "spot/core/log.h"
#include "spot/import/importers/importer.h"

namespace Spot::Detail {

bool ImportAnimatorController(ImportContext& context) {
    const auto bytes = ReadFileBytes(context.GetSourcePath());
    const auto text =
        bytes ? std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()) : std::string_view{};
    const auto controller = AnimatorController::LoadFromString(text);
    if (!controller) {
        Log::GetEngineLogger().Error("Invalid animator controller source");
        return false;
    }
    BinaryWriter writer;
    EncodeScene(writer, controller->SaveToString());
    return context.WriteAsset(AssetType::AnimatorController, writer);
}

} // namespace Spot::Detail
