/*
    json/error.hpp
    ----------------
    Defines error handling structures and enums for JSON parsing.
*/

#pragma once

#include <string>
#include <expected>

namespace json {

    // Error codes for JSON parsing
    enum class ErrorCode {
        None,
        UnexpectedEnd,
        UnexpectedToken,
        InvalidNumber,
        InvalidString,
        InvalidEscape,
        InvalidUnicode,
        DepthLimitExceeded,
        NodeLimitExceeded,
        KeyTooLong,
        StringTooLong,
        DuplicateKey,
    };

    struct ParseError {
        ErrorCode code = ErrorCode::None;
        size_t position = 0;
        std::string message;

        // Constructor with all fields
        ParseError(ErrorCode c, std::string msg, size_t pos = 0)
            : code(c), position(pos), message(std::move(msg)) {}

        // Constructor with message only
        ParseError(std::string msg, size_t pos = 0)
            : code(ErrorCode::None), position(pos), message(std::move(msg)) {}
    };

    template<typename T>
    using Expected = std::expected<T, ParseError>;

}
