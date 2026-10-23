#include "import/importers/importer.h"

#include "core/file_io.h"

#include <string_view>

namespace Spot::Detail {

// Import source without depending on a language runtime. Parsing happens when a component starts.
bool ImportScript(ImportContext& context) {
    const auto bytes = ReadFileBytes(context.GetSourcePath());
    if (!bytes) {
        return false;
    }
    std::string_view text = bytes->empty()
                                ? std::string_view{}
                                : std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    if (text.starts_with("\xEF\xBB\xBF")) {
        text.remove_prefix(3);
    }
    BinaryWriter data;
    data.WriteString(text);
    return context.WriteAsset(AssetType::Script, data);
}

} // namespace Spot::Detail
