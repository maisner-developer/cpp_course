#include "../include/json/parser.hpp"
#include "../include/json/value.hpp"
#include "../include/json/tokenizer.hpp"

#include <charconv>
#include <iostream>

namespace json {

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

} // namespace json
