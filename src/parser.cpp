#include "../include/json/parser.hpp"
#include "../include/json/value.hpp"
#include "../include/json/tokenizer.hpp"

#include <charconv>
#include <iostream>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <thread>
#include <vector>
#include <utility>

namespace json {

namespace {

inline bool is_ws(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

std::string_view trim_view(std::string_view sv) {
    size_t start = 0;
    size_t end = sv.size();

    while (start < end && is_ws(sv[start])) ++start;
    while (end > start && is_ws(sv[end - 1])) --end;

    return sv.substr(start, end - start);
}

ParseResult make_unexpected_end() {
    return ParseError{ErrorCode::UnexpectedEnd, "Unexpected end of input"};
}

ParseResult make_unexpected_token(const char* msg) {
    return ParseError{ErrorCode::UnexpectedToken, msg};
}

bool split_root_array(std::string_view input,
                      size_t start_pos,
                      std::vector<std::string_view>& out,
                      size_t& end_pos,
                      ParseResult& error) {
    bool in_string = false;
    bool escape = false;
    int depth = 1;
    size_t elem_start = start_pos + 1;

    for (size_t i = start_pos + 1; i < input.size(); ++i) {
        char c = input[i];

        if (in_string) {
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }

        if (c == '"') {
            in_string = true;
            continue;
        }

        if (c == '[' || c == '{') {
            ++depth;
            continue;
        }

        if (c == ']' || c == '}') {
            --depth;
            if (depth == 0) {
                std::string_view sv = trim_view(input.substr(elem_start, i - elem_start));
                if (!sv.empty()) {
                    out.push_back(sv);
                } else if (!out.empty()) {
                    error = make_unexpected_token("Expected array element");
                    return false;
                }

                end_pos = i;
                return true;
            }
            if (depth < 0) {
                error = make_unexpected_token("Unexpected closing bracket");
                return false;
            }
            continue;
        }

        if (c == ',' && depth == 1) {
            std::string_view sv = trim_view(input.substr(elem_start, i - elem_start));
            if (sv.empty()) {
                error = make_unexpected_token("Expected array element");
                return false;
            }
            out.push_back(sv);
            elem_start = i + 1;
        }
    }

    error = make_unexpected_end();
    return false;
}

bool split_root_object(std::string_view input,
                       size_t start_pos,
                       std::vector<std::string_view>& out,
                       size_t& end_pos,
                       ParseResult& error) {
    bool in_string = false;
    bool escape = false;
    int depth = 1;
    size_t pair_start = start_pos + 1;

    for (size_t i = start_pos + 1; i < input.size(); ++i) {
        char c = input[i];

        if (in_string) {
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }

        if (c == '"') {
            in_string = true;
            continue;
        }

        if (c == '[' || c == '{') {
            ++depth;
            continue;
        }

        if (c == ']' || c == '}') {
            --depth;
            if (depth == 0) {
                std::string_view sv = trim_view(input.substr(pair_start, i - pair_start));
                if (!sv.empty()) {
                    out.push_back(sv);
                } else if (!out.empty()) {
                    error = make_unexpected_token("Expected object pair");
                    return false;
                }

                end_pos = i;
                return true;
            }
            if (depth < 0) {
                error = make_unexpected_token("Unexpected closing brace");
                return false;
            }
            continue;
        }

        if (c == ',' && depth == 1) {
            std::string_view sv = trim_view(input.substr(pair_start, i - pair_start));
            if (sv.empty()) {
                error = make_unexpected_token("Expected object pair");
                return false;
            }
            out.push_back(sv);
            pair_start = i + 1;
        }
    }

    error = make_unexpected_end();
    return false;
}

ParseLimits adjust_limits_for_root_children(const ParseLimits& limits) {
    ParseLimits sub = limits;
    if (sub.max_depth >= 2) {
        sub.max_depth -= 2;
    } else {
        sub.max_depth = 0;
    }
    return sub;
}

std::string make_part_error_message(const ParseError& err, size_t index, std::string_view part) {
    constexpr size_t kMaxSnippet = 120;
    std::string snippet;
    size_t take = std::min(part.size(), kMaxSnippet);
    snippet.assign(part.data(), part.data() + take);
    if (part.size() > kMaxSnippet)
        snippet += "...";

    std::string msg = err.message;
    msg += " (root item index: ";
    msg += std::to_string(index);
    msg += ", snippet: \"";
    msg += snippet;
    msg += "\")";
    return msg;
}

} // namespace

namespace detail {

//  Heaplers

static inline Token expect(Tokenizer& tokenizer, TokenType t, const char* msg)
{
    Token tok = tokenizer.next_token();
    if (tok.type != t)
        return Token{TokenType::Error, msg};
    return tok;
}

ParseResult parse_value(Tokenizer&, const ParseLimits&, size_t, size_t&);
ParseResult parse_object(Tokenizer&, const ParseLimits&, size_t, size_t&);
ParseResult parse_array(Tokenizer&, const ParseLimits&, size_t, size_t&);

static inline ParseResult make_limit_error(ErrorCode code, const char* msg)
{
    return ParseError{code, msg};
}

static inline bool try_add_node(const ParseLimits& limits, size_t& node_count)
{
    if (node_count + 1 > limits.max_total_nodes)
        return false;
    ++node_count;
    return true;
}

//  ARRAY

ParseResult parse_array(Tokenizer& tokenizer, const ParseLimits& limits, size_t depth, size_t& node_count)
{
    Array arr;

    if (!try_add_node(limits, node_count))
        return make_limit_error(ErrorCode::NodeLimitExceeded, "Node limit exceeded");

    // смотрим первый токен после '['
    Token tok = tokenizer.peek_token();
    // std::cout << "[DEBUG] Parsing value, token type: " << static_cast<int>(tok.type) << "\n";

    // []
    if (tok.type == TokenType::RightBracket) {
        tokenizer.next_token(); // потребить ]
        return Value{std::move(arr)};
    }

    if (tok.type == TokenType::EndOfStream || tok.type == TokenType::NeedMoreData)
        return ParseError{ErrorCode::UnexpectedEnd, "Unexpected end of input"};

    if (tok.type == TokenType::Error)
        return ParseError{tok.error_code, tok.error_message};

    while (true) {

        // значение парсится через parse_value
        ParseResult element = parse_value(tokenizer, limits, depth + 1, node_count);

        if (std::holds_alternative<ParseError>(element))
            return std::get<ParseError>(element);

        arr.push_back(std::get<Value>(element));

        // ожидаем ',' или ']'
        Token sep = tokenizer.next_token();

        if (sep.type == TokenType::RightBracket)
            break;

        if (sep.type == TokenType::EndOfStream || sep.type == TokenType::NeedMoreData)
            return ParseError{ErrorCode::UnexpectedEnd, "Unexpected end of input"};

        if (sep.type == TokenType::Error)
            return ParseError{sep.error_code, sep.error_message};

        if (sep.type != TokenType::Comma)
            return ParseError{"Expected ',' or ']' after array element"};
    }

    return Value{std::move(arr)};
}

//  OBJECT

ParseResult parse_object(Tokenizer& tokenizer, const ParseLimits& limits, size_t depth, size_t& node_count)
{
    Object obj;

    if (!try_add_node(limits, node_count))
        return make_limit_error(ErrorCode::NodeLimitExceeded, "Node limit exceeded");

    Token tok = tokenizer.peek_token();

    // std::cout << "[DEBUG] Parsing value, token type: " << static_cast<int>(tok.type) << "\n";

    // {} пустой объект
    if (tok.type == TokenType::RightBrace) {
        tokenizer.next_token();
        return Value{std::move(obj)};
    }

    if (tok.type == TokenType::EndOfStream || tok.type == TokenType::NeedMoreData)
        return ParseError{ErrorCode::UnexpectedEnd, "Unexpected end of input"};

    if (tok.type == TokenType::Error)
        return ParseError{tok.error_code, tok.error_message};

    while (true) {

        Token keyTok = tokenizer.next_token();
        if (keyTok.type == TokenType::EndOfStream || keyTok.type == TokenType::NeedMoreData)
            return ParseError{ErrorCode::UnexpectedEnd, "Unexpected end of input"};

        if (keyTok.type == TokenType::Error)
            return ParseError{keyTok.error_code, keyTok.error_message};

        if (keyTok.type != TokenType::String)
            return ParseError{"Expected string key in object"};

        std::string key = keyTok.value;

        if (key.size() > limits.max_key_length)
            return make_limit_error(ErrorCode::KeyTooLong, "Object key length exceeds limit");

        Token colon = tokenizer.next_token();
        if (colon.type == TokenType::EndOfStream || colon.type == TokenType::NeedMoreData)
            return ParseError{ErrorCode::UnexpectedEnd, "Unexpected end of input"};

        if (colon.type == TokenType::Error)
            return ParseError{colon.error_code, colon.error_message};

        if (colon.type != TokenType::Colon)
            return ParseError{"Expected ':' after key in object"};

        // рекурсивный парс значения
        ParseResult valRes = parse_value(tokenizer, limits, depth + 1, node_count);

        if (std::holds_alternative<ParseError>(valRes))
            return std::get<ParseError>(valRes);

        if (limits.forbid_duplicate_keys && obj.find(key) != obj.end())
            return make_limit_error(ErrorCode::DuplicateKey, "Duplicate key in object");

        obj.emplace(std::move(key), std::get<Value>(valRes));

        Token sep = tokenizer.next_token();

        if (sep.type == TokenType::RightBrace)
            break;

        if (sep.type == TokenType::EndOfStream || sep.type == TokenType::NeedMoreData)
            return ParseError{ErrorCode::UnexpectedEnd, "Unexpected end of input"};

        if (sep.type == TokenType::Error)
            return ParseError{sep.error_code, sep.error_message};

        if (sep.type != TokenType::Comma)
            return ParseError{"Expected ',' or '}' after object pair"};
    }

    return Value{std::move(obj)};
}

//  VALUE (general case)

ParseResult parse_value(Tokenizer& tokenizer, const ParseLimits& limits, size_t depth, size_t& node_count)
{
    if (depth > limits.max_depth)
        return make_limit_error(ErrorCode::DepthLimitExceeded, "Max depth exceeded");

    Token tok = tokenizer.next_token();

    // std::cout << "[DEBUG] Parsing value, token type: " << static_cast<int>(tok.type) << "\n";

    switch (tok.type) {

        case TokenType::LeftBrace:
            return parse_object(tokenizer, limits, depth + 1, node_count);

        case TokenType::LeftBracket:
            return parse_array(tokenizer, limits, depth + 1, node_count);

        case TokenType::String:
            if (tok.value.size() > limits.max_string_length)
                return make_limit_error(ErrorCode::StringTooLong, "String length exceeds limit");
            if (!try_add_node(limits, node_count))
                return make_limit_error(ErrorCode::NodeLimitExceeded, "Node limit exceeded");
            return Value{tok.value};

        case TokenType::Number: {
            double x = 0;
            
            auto res = std::from_chars(tok.value.data(), tok.value.data() + tok.value.size(), x);
            if (res.ec != std::errc{} || res.ptr != tok.value.data() + tok.value.size())
                return ParseError{ErrorCode::InvalidNumber, "Invalid number"};

            if (!try_add_node(limits, node_count))
                return make_limit_error(ErrorCode::NodeLimitExceeded, "Node limit exceeded");
            return Value{x};
        }

        case TokenType::True:
            if (!try_add_node(limits, node_count))
                return make_limit_error(ErrorCode::NodeLimitExceeded, "Node limit exceeded");
            return Value{true};

        case TokenType::False:
            if (!try_add_node(limits, node_count))
                return make_limit_error(ErrorCode::NodeLimitExceeded, "Node limit exceeded");
            return Value{false};

        case TokenType::Null:
            if (!try_add_node(limits, node_count))
                return make_limit_error(ErrorCode::NodeLimitExceeded, "Node limit exceeded");
            return Value{nullptr};

        case TokenType::NeedMoreData:
            return ParseError{ErrorCode::UnexpectedEnd, "Unexpected end of input"};

        case TokenType::EndOfStream:
            return ParseError{ErrorCode::UnexpectedEnd, "Unexpected end of input"};

        case TokenType::Error:
            return ParseError{tok.error_code, tok.error_message};

        default:
            return ParseError{ErrorCode::UnexpectedToken, "Unexpected token while parsing value"};
    }
}

} // namespace detail

// =====================
//  PUBLIC API
// =====================

ParseResult parse_json(std::string_view input, const ParseLimits& limits)
{
    Tokenizer tokenizer;
    tokenizer.push_chunk(input);
    tokenizer.end_input();

    size_t node_count = 0;
    ParseResult root = detail::parse_value(tokenizer, limits, 0, node_count);

    if (std::holds_alternative<ParseError>(root))
        return root;

    Token tail = tokenizer.next_token();
    if (tail.type == TokenType::EndOfStream)
        return root;

    if (tail.type == TokenType::Error)
        return ParseError{ErrorCode::UnexpectedToken, tail.error_message};

    if (tail.type == TokenType::NeedMoreData)
        return ParseError{ErrorCode::UnexpectedEnd, "Unexpected end of input"};

    return ParseError{ErrorCode::UnexpectedToken, "Unexpected trailing token"};
}

ParseResult parse_json_parallel(std::string_view input,
                                size_t threads,
                                size_t batch_size,
                                const ParseLimits& limits)
{
    if (threads <= 1)
        return parse_json(input, limits);

    size_t first = 0;
    while (first < input.size() && is_ws(input[first])) ++first;
    if (first >= input.size())
        return parse_json(input, limits);

    char root = input[first];
    if (root != '[' && root != '{')
        return parse_json(input, limits);

    std::vector<std::string_view> parts;
    size_t end_pos = 0;
    ParseResult split_err = ParseError{ErrorCode::None, ""};

    bool ok = false;
    if (root == '[') {
        ok = split_root_array(input, first, parts, end_pos, split_err);
    } else {
        ok = split_root_object(input, first, parts, end_pos, split_err);
    }

    if (!ok)
        return split_err;

    for (size_t i = end_pos + 1; i < input.size(); ++i) {
        if (!is_ws(input[i]))
            return ParseError{ErrorCode::UnexpectedToken, "Unexpected trailing token"};
    }

    ParseLimits sub_limits = adjust_limits_for_root_children(limits);

    if (root == '[') {
        std::vector<ParseResult> results(parts.size());
        std::atomic<size_t> next{0};

        auto worker = [&]() {
            while (true) {
                size_t start = next.fetch_add(batch_size, std::memory_order_relaxed);
                if (start >= parts.size())
                    return;
                size_t end = std::min(start + batch_size, parts.size());
                for (size_t i = start; i < end; ++i) {
                    results[i] = parse_json(parts[i], sub_limits);
                }
            }
        };

        std::vector<std::thread> workers;
        workers.reserve(threads);
        for (size_t i = 0; i < threads; ++i)
            workers.emplace_back(worker);

        for (auto& t : workers)
            t.join();

        Array arr;
        arr.reserve(parts.size());
        for (size_t i = 0; i < results.size(); ++i) {
            if (std::holds_alternative<ParseError>(results[i])) {
                const auto& err = std::get<ParseError>(results[i]);
                return ParseError{err.code, make_part_error_message(err, i, parts[i]), err.position};
            }
            arr.push_back(std::get<Value>(std::move(results[i])));
        }

        return Value{std::move(arr)};
    }

    std::vector<ParseResult> results(parts.size());
    std::atomic<size_t> next{0};

    auto worker = [&]() {
        while (true) {
            size_t start = next.fetch_add(batch_size, std::memory_order_relaxed);
            if (start >= parts.size())
                return;
            size_t end = std::min(start + batch_size, parts.size());
            for (size_t i = start; i < end; ++i) {
                std::string wrapped;
                wrapped.reserve(parts[i].size() + 2);
                wrapped.push_back('{');
                wrapped.append(parts[i].data(), parts[i].size());
                wrapped.push_back('}');
                results[i] = parse_json(wrapped, sub_limits);
            }
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(threads);
    for (size_t i = 0; i < threads; ++i)
        workers.emplace_back(worker);

    for (auto& t : workers)
        t.join();

    Object obj;
    for (size_t i = 0; i < results.size(); ++i) {
        if (std::holds_alternative<ParseError>(results[i])) {
            const auto& err = std::get<ParseError>(results[i]);
            return ParseError{err.code, make_part_error_message(err, i, parts[i]), err.position};
        }

        Value v = std::get<Value>(std::move(results[i]));
        if (!v.is_object())
            return make_unexpected_token("Expected object pair");

        const auto& sub_obj = v.as_object()->get();
        if (sub_obj.size() != 1)
            return make_unexpected_token("Expected object pair");

        const auto& [k, val] = *sub_obj.begin();
        if (limits.forbid_duplicate_keys && obj.find(k) != obj.end())
            return ParseError{ErrorCode::DuplicateKey, "Duplicate key in object"};

        obj.emplace(k, val);
    }

    return Value{std::move(obj)};
}

} // namespace json
