#pragma once

#include "scene/component.h"

#include <string>
#include <utility>

namespace Spot {

// Stands in for a component whose type is not registered, such as one from game code that is not loaded, so saving
// its scene again keeps the component as it was. It does nothing else.
class MissingComponent final : public Component {
public:
    MissingComponent(std::string typeName, std::string data)
        : m_typeName(std::move(typeName)), m_data(std::move(data)) {}

    // The name the missing type was saved under.
    const std::string& GetTypeName() const { return m_typeName; }
    // The component's saved fields, as JSON.
    const std::string& GetData() const { return m_data; }

private:
    friend struct Detail::SceneSerializer;

    std::string m_typeName;
    std::string m_data;
};

} // namespace Spot
