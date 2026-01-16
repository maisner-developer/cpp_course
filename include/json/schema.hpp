#pragma once

#include "value.hpp"
#include "error.hpp"

#include <string>
#include <variant>

namespace json {

    struct SchemaError {
        std::string path;   // JSON Pointer to the failing instance location
        std::string message;
    };

    using SchemaResult = std::variant<std::monostate, SchemaError>;

    // Validate JSON instance against JSON Schema (MVP: type, required, properties, items, enum)
    SchemaResult validate_schema(const Value& schema, const Value& instance);

}
