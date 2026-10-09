#pragma once

#include <exception>
#include <string>
#include <typeinfo>

namespace Spot::Detail {

// The message of a std::exception, or a description of an exception of another type, for logs.
std::string DescribeException(const std::exception_ptr& error);

// The type's name as written in code, such as "Spot::PhysicsWorld", for logs.
std::string GetTypeName(const std::type_info& type);

} // namespace Spot::Detail
