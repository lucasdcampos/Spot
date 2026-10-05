#include "graphics/pipeline.h"

namespace Spot {

std::uint32_t GetVertexFormatSize(VertexFormat format) {
    switch (format) {
    case VertexFormat::Float:
        return 4;
    case VertexFormat::Float2:
        return 8;
    case VertexFormat::Float3:
        return 12;
    case VertexFormat::Float4:
        return 16;
    case VertexFormat::UByte4Normalized:
        return 4;
    }
    return 0;
}

} // namespace Spot
