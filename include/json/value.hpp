/*
    json/value.hpp
    ----------------
    JSON Value representation

    This file defines the Value struct and related types for representing JSON values.
*/

#pragma once

#include <variant>
#include <map>
#include <vector>
#include <string>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <expected>

namespace json {

    class Value;

    using Object = std::map<std::string, Value>;
    using Array  = std::vector<Value>;

    class Value {
    public:

        using Variant = std::variant<
            std::nullptr_t,
            bool,
            double,
            std::string,
            Array,
            Object
        >;

    private:
        Variant data_;

    public:

        // constructors
        #pragma region Constructors

        Value() : data_(nullptr) {}
        Value(std::nullptr_t) : data_(nullptr) {}
        Value(bool b) : data_(b) {}
        Value(double d) : data_(d) {}
        Value(const char* s) : data_(std::string(s)) {}
        Value(std::string s) : data_(std::move(s)) {}
        Value(Array a) : data_(std::move(a)) {}
        Value(Object o) : data_(std::move(o)) {}

        #pragma endregion

        // type checks
        #pragma region TypeChecks

        bool is_null()   const { return std::holds_alternative<std::nullptr_t>(data_); }
        bool is_bool()   const { return std::holds_alternative<bool>(data_); }
        bool is_number() const { return std::holds_alternative<double>(data_); }
        bool is_string() const { return std::holds_alternative<std::string>(data_); }
        bool is_array()  const { return std::holds_alternative<Array>(data_); }
        bool is_object() const { return std::holds_alternative<Object>(data_); }

        #pragma endregion

        // safe accessors
        #pragma region Accessors

        std::optional<std::reference_wrapper<const std::string>> as_string() const {
            if (!is_string()) return std::nullopt;
            return std::cref(std::get<std::string>(data_));
        }

        std::optional<std::reference_wrapper<const double>> as_number() const {
            if (!is_number()) return std::nullopt;
            return std::cref(std::get<double>(data_));
        }

        std::optional<std::reference_wrapper<const bool>> as_bool() const {
            if (!is_bool()) return std::nullopt;
            return std::cref(std::get<bool>(data_));
        }

        std::optional<std::reference_wrapper<const Array>> as_array() const {
            if (!is_array()) return std::nullopt;
            return std::cref(std::get<Array>(data_));
        }

        std::optional<std::reference_wrapper<const Object>> as_object() const {
            if (!is_object()) return std::nullopt;
            return std::cref(std::get<Object>(data_));
        }
        
        #pragma endregion

        #pragma region Getters

        template<typename T>
        T get() const {
            if constexpr (std::is_same_v<T, std::string>) {
                if (!is_string()) throw std::runtime_error("Value is not string");
                return std::get<std::string>(data_);
            }
            else if constexpr (std::is_same_v<T, double>) {
                if (!is_number()) throw std::runtime_error("Value is not number");
                return std::get<double>(data_);
            }
            else if constexpr (std::is_same_v<T, bool>) {
                if (!is_bool()) throw std::runtime_error("Value is not bool");
                return std::get<bool>(data_);
            }
            else {
                static_assert(!sizeof(T), "Unsupported type in json::Value::get()");
            }
        }

        #pragma endregion
        
        #pragma region Operators

        #pragma region Navigation

        // const version
        const Value& operator[](std::string_view key) const {
            if (!is_object())
                throw std::runtime_error("Value is not an object");

            const auto& obj = std::get<Object>(data_);
            auto it = obj.find(std::string(key));

            if (it == obj.end())
                throw std::runtime_error("Key not found in object");

            return it->second;
        }

        // array indexing
        const Value& operator[](size_t index) const {
            if (!is_array())
                throw std::runtime_error("Value is not an array");

            const auto& arr = std::get<Array>(data_);

            if (index >= arr.size())
                throw std::runtime_error("Index out of range");

            return arr[index];
        }

        #pragma endregion

        friend std::ostream& operator<<(std::ostream& os, const Value& v) {
            std::visit([&os](auto const& val) {

                using T = std::decay_t<decltype(val)>;

                if constexpr (std::is_same_v<T, std::nullptr_t>) {
                    os << "null";
                }
                else if constexpr (std::is_same_v<T, bool>) {
                    os << (val ? "true" : "false");
                }
                else if constexpr (std::is_same_v<T, double>) {
                    os << val;
                }
                else if constexpr (std::is_same_v<T, std::string>) {
                    os << '"' << val << '"';
                }
                else if constexpr (std::is_same_v<T, Array>) {
                    os << "[";
                    bool first = true;
                    for (auto const& e : val) {
                        if (!first) os << ", ";
                        first = false;
                        os << e;
                    }
                    os << "]";
                }
                else if constexpr (std::is_same_v<T, Object>) {
                    os << "{";
                    bool first = true;
                    for (auto const& [k, v2] : val) {
                        if (!first) os << ", ";
                        first = false;
                        os << '"' << k << "\": " << v2;
                    }
                    os << "}";
                }

            }, v.data_);

            return os;
        }

        #pragma endregion
    };

}
