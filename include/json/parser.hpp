/*
    json/parser.hpp
    ----------------
    Declares the JSON parsing function and related structures.
*/

#pragma once

#include "value.hpp"
#include "error.hpp"
#include "tokenizer.hpp"
#include <string_view>

namespace json {

    // Limits for JSON parsing
    struct ParseLimits {
        size_t max_depth = 64;
        size_t max_total_nodes = 10'000;
        size_t max_key_length = 32'768;
        size_t max_string_length = 65'536;
        bool forbid_duplicate_keys = true;
    };

    Expected<Value> parse(std::string_view input,
                          const ParseLimits& limits = ParseLimits{}, size_t depth = 0);

    // Parse result type
    using ParseResult = std::variant<Value, ParseError>;

    // Public API function to parse JSON input
    ParseResult parse_json(std::string_view input, const ParseLimits& limits = {});

    namespace detail {
        ParseResult parse_object(Tokenizer& tokenizer, const ParseLimits& limits, size_t depth, size_t& node_count);
        ParseResult parse_array(Tokenizer& tokenizer, const ParseLimits& limits, size_t depth, size_t& node_count);
        ParseResult parse_value(Tokenizer& tokenizer, const ParseLimits& limits, size_t depth, size_t& node_count);
    } // namespace detail

}
