#include "../include/json/schema.hpp"

#include <string>
#include <vector>

namespace json {

static std::string escape_json_pointer(std::string_view token) {
    std::string out;
    out.reserve(token.size());
    for (char c : token) {
        if (c == '~') {
            out += "~0";
        } else if (c == '/') {
            out += "~1";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

static SchemaResult make_error(std::string path, std::string message) {
    return SchemaError{std::move(path), std::move(message)};
}

static const Value* get_prop(const Value& objVal, std::string_view key) {
    auto objOpt = objVal.as_object();
    if (!objOpt)
        return nullptr;
    const auto& obj = objOpt->get();
    auto it = obj.find(std::string(key));
    if (it == obj.end())
        return nullptr;
    return &it->second;
}

static bool value_equals(const Value& a, const Value& b);

static bool array_equals(const Array& a, const Array& b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (!value_equals(a[i], b[i]))
            return false;
    }
    return true;
}

static bool object_equals(const Object& a, const Object& b) {
    if (a.size() != b.size())
        return false;
    for (const auto& [k, v] : a) {
        auto it = b.find(k);
        if (it == b.end())
            return false;
        if (!value_equals(v, it->second))
            return false;
    }
    return true;
}

static bool value_equals(const Value& a, const Value& b) {
    if (a.is_null() != b.is_null()) return false;
    if (a.is_bool() != b.is_bool()) return false;
    if (a.is_number() != b.is_number()) return false;
    if (a.is_string() != b.is_string()) return false;
    if (a.is_array() != b.is_array()) return false;
    if (a.is_object() != b.is_object()) return false;

    if (a.is_null()) return true;
    if (a.is_bool()) return a.get<bool>() == b.get<bool>();
    if (a.is_number()) return a.get<double>() == b.get<double>();
    if (a.is_string()) return a.get<std::string>() == b.get<std::string>();
    if (a.is_array()) return array_equals(a.as_array()->get(), b.as_array()->get());
    if (a.is_object()) return object_equals(a.as_object()->get(), b.as_object()->get());

    return false;
}

static bool type_matches(std::string_view typeName, const Value& instance) {
    if (typeName == "null") return instance.is_null();
    if (typeName == "boolean") return instance.is_bool();
    if (typeName == "number") return instance.is_number();
    if (typeName == "string") return instance.is_string();
    if (typeName == "array") return instance.is_array();
    if (typeName == "object") return instance.is_object();
    return false;
}

static bool get_number(const Value& v, double& out) {
    if (!v.is_number())
        return false;
    out = v.get<double>();
    return true;
}

static bool get_uint(const Value& v, size_t& out) {
    if (!v.is_number())
        return false;
    double d = v.get<double>();
    if (d < 0)
        return false;
    out = static_cast<size_t>(d);
    return static_cast<double>(out) == d;
}

static SchemaResult validate_impl(const Value& schema, const Value& instance, const std::string& path) {
    auto schemaObj = schema.as_object();
    if (!schemaObj)
        return make_error(path, "Schema must be an object");

    // type
    if (const Value* typeVal = get_prop(schema, "type")) {
        bool ok = false;
        if (typeVal->is_string()) {
            ok = type_matches(typeVal->get<std::string>(), instance);
        } else if (typeVal->is_array()) {
            const auto& arr = typeVal->as_array()->get();
            for (const auto& v : arr) {
                if (v.is_string() && type_matches(v.get<std::string>(), instance)) {
                    ok = true;
                    break;
                }
            }
        } else {
            return make_error(path, "Schema 'type' must be string or array of strings");
        }

        if (!ok)
            return make_error(path, "Type mismatch");
    }

    // enum
    if (const Value* enumVal = get_prop(schema, "enum")) {
        if (!enumVal->is_array())
            return make_error(path, "Schema 'enum' must be an array");

        const auto& arr = enumVal->as_array()->get();
        bool found = false;
        for (const auto& v : arr) {
            if (value_equals(v, instance)) {
                found = true;
                break;
            }
        }
        if (!found)
            return make_error(path, "Value not in enum");
    }

    // required
    if (const Value* reqVal = get_prop(schema, "required")) {
        if (!reqVal->is_array())
            return make_error(path, "Schema 'required' must be an array");
        if (!instance.is_object())
            return make_error(path, "Expected object for 'required'");

        const auto& reqArr = reqVal->as_array()->get();
        const auto& obj = instance.as_object()->get();
        for (const auto& v : reqArr) {
            if (!v.is_string())
                return make_error(path, "Schema 'required' must contain only strings");
            std::string key = v.get<std::string>();
            if (obj.find(key) == obj.end()) {
                std::string p = path + "/" + escape_json_pointer(key);
                return make_error(p, "Missing required property");
            }
        }
    }

    // properties
    if (const Value* propsVal = get_prop(schema, "properties")) {
        if (!propsVal->is_object())
            return make_error(path, "Schema 'properties' must be an object");
        if (!instance.is_object())
            return make_error(path, "Expected object for 'properties'");

        const auto& props = propsVal->as_object()->get();
        const auto& obj = instance.as_object()->get();
        for (const auto& [key, propSchema] : props) {
            auto it = obj.find(key);
            if (it == obj.end())
                continue;
            std::string p = path + "/" + escape_json_pointer(key);
            SchemaResult r = validate_impl(propSchema, it->second, p);
            if (std::holds_alternative<SchemaError>(r))
                return r;
        }
    }

    // items
    if (const Value* itemsVal = get_prop(schema, "items")) {
        if (!instance.is_array())
            return make_error(path, "Expected array for 'items'");

        const auto& arr = instance.as_array()->get();
        for (size_t i = 0; i < arr.size(); ++i) {
            std::string p = path + "/" + std::to_string(i);
            SchemaResult r = validate_impl(*itemsVal, arr[i], p);
            if (std::holds_alternative<SchemaError>(r))
                return r;
        }
    }

    // numeric constraints
    if (const Value* minVal = get_prop(schema, "minimum")) {
        double minv = 0;
        if (!get_number(*minVal, minv))
            return make_error(path, "Schema 'minimum' must be a number");
        if (!instance.is_number())
            return make_error(path, "Expected number for 'minimum'");
        if (instance.get<double>() < minv)
            return make_error(path, "Number is less than minimum");
    }

    if (const Value* maxVal = get_prop(schema, "maximum")) {
        double maxv = 0;
        if (!get_number(*maxVal, maxv))
            return make_error(path, "Schema 'maximum' must be a number");
        if (!instance.is_number())
            return make_error(path, "Expected number for 'maximum'");
        if (instance.get<double>() > maxv)
            return make_error(path, "Number is greater than maximum");
    }

    if (const Value* minExVal = get_prop(schema, "exclusiveMinimum")) {
        double minv = 0;
        if (!get_number(*minExVal, minv))
            return make_error(path, "Schema 'exclusiveMinimum' must be a number");
        if (!instance.is_number())
            return make_error(path, "Expected number for 'exclusiveMinimum'");
        if (instance.get<double>() <= minv)
            return make_error(path, "Number is not greater than exclusiveMinimum");
    }

    if (const Value* maxExVal = get_prop(schema, "exclusiveMaximum")) {
        double maxv = 0;
        if (!get_number(*maxExVal, maxv))
            return make_error(path, "Schema 'exclusiveMaximum' must be a number");
        if (!instance.is_number())
            return make_error(path, "Expected number for 'exclusiveMaximum'");
        if (instance.get<double>() >= maxv)
            return make_error(path, "Number is not less than exclusiveMaximum");
    }

    // string constraints
    if (const Value* minLenVal = get_prop(schema, "minLength")) {
        size_t minLen = 0;
        if (!get_uint(*minLenVal, minLen))
            return make_error(path, "Schema 'minLength' must be a non-negative integer");
        if (!instance.is_string())
            return make_error(path, "Expected string for 'minLength'");
        if (instance.get<std::string>().size() < minLen)
            return make_error(path, "String is shorter than minLength");
    }

    if (const Value* maxLenVal = get_prop(schema, "maxLength")) {
        size_t maxLen = 0;
        if (!get_uint(*maxLenVal, maxLen))
            return make_error(path, "Schema 'maxLength' must be a non-negative integer");
        if (!instance.is_string())
            return make_error(path, "Expected string for 'maxLength'");
        if (instance.get<std::string>().size() > maxLen)
            return make_error(path, "String is longer than maxLength");
    }

    // array constraints
    if (const Value* minItemsVal = get_prop(schema, "minItems")) {
        size_t minItems = 0;
        if (!get_uint(*minItemsVal, minItems))
            return make_error(path, "Schema 'minItems' must be a non-negative integer");
        if (!instance.is_array())
            return make_error(path, "Expected array for 'minItems'");
        if (instance.as_array()->get().size() < minItems)
            return make_error(path, "Array has fewer items than minItems");
    }

    if (const Value* maxItemsVal = get_prop(schema, "maxItems")) {
        size_t maxItems = 0;
        if (!get_uint(*maxItemsVal, maxItems))
            return make_error(path, "Schema 'maxItems' must be a non-negative integer");
        if (!instance.is_array())
            return make_error(path, "Expected array for 'maxItems'");
        if (instance.as_array()->get().size() > maxItems)
            return make_error(path, "Array has more items than maxItems");
    }

    // object constraints
    if (const Value* minPropsVal = get_prop(schema, "minProperties")) {
        size_t minProps = 0;
        if (!get_uint(*minPropsVal, minProps))
            return make_error(path, "Schema 'minProperties' must be a non-negative integer");
        if (!instance.is_object())
            return make_error(path, "Expected object for 'minProperties'");
        if (instance.as_object()->get().size() < minProps)
            return make_error(path, "Object has fewer properties than minProperties");
    }

    if (const Value* maxPropsVal = get_prop(schema, "maxProperties")) {
        size_t maxProps = 0;
        if (!get_uint(*maxPropsVal, maxProps))
            return make_error(path, "Schema 'maxProperties' must be a non-negative integer");
        if (!instance.is_object())
            return make_error(path, "Expected object for 'maxProperties'");
        if (instance.as_object()->get().size() > maxProps)
            return make_error(path, "Object has more properties than maxProperties");
    }

    return std::monostate{};
}

SchemaResult validate_schema(const Value& schema, const Value& instance) {
    return validate_impl(schema, instance, "");
}

} // namespace json
