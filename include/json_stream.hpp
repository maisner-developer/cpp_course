#pragma once
#include <string>
#include <variant>
#include <functional>
#include <vector>

#include "json/tokenizer.hpp"
#include "json/error.hpp"

namespace json {

enum class JsonEventType {
    StartObject,
    EndObject,
    StartArray,
    EndArray,
    Key,
    String,
    Number,
    Boolean,
    Null
};

struct JsonEvent {
    JsonEventType type;
    std::string value; // key or primitive
};

using JsonEventHandler = std::function<void(const JsonEvent&)>;

class JsonStreamParser {
public:
    explicit JsonStreamParser(JsonEventHandler handler);

    void feed(std::string_view chunk);
    void finalize();

private:
    void process_tokens();
    void set_error(ErrorCode code, const std::string& message);

    enum class ContainerType {
        Object,
        Array
    };

    enum class Expectation {
        KeyOrEnd,
        Colon,
        ValueOrEnd,
        CommaOrEnd,
        Value
    };

    struct ContainerState {
        ContainerType type;
        Expectation expect;
    };

    Tokenizer tokenizer_;
    std::vector<ContainerState> stack_;
    bool root_complete_ = false;
    bool has_error_ = false;
    ParseError error_ = ParseError{ErrorCode::None, ""};

    JsonEventHandler handler_;
};

} // namespace json
