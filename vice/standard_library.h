#pragma once

#include <vice/vice.h>

namespace Vice::Detail {

std::unordered_map<std::string, ObjectRef> CreateStandardLibraries();

} // namespace Vice::Detail
