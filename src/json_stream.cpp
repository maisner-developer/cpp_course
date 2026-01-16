#include "../include/json_stream.hpp"

namespace json {

JsonStreamParser::JsonStreamParser(JsonEventHandler handler)
    : handler_(std::move(handler)) {}

void JsonStreamParser::feed(std::string_view chunk) {
    tokenizer_.push_chunk(chunk);
    process_tokens();
}

void JsonStreamParser::finalize() {
    tokenizer_.end_input();
    process_tokens();

    if (!has_error_ && (!root_complete_ || !stack_.empty())) {
        set_error(ErrorCode::UnexpectedEnd, "Unexpected end of input");
    }
}

void JsonStreamParser::set_error(ErrorCode code, const std::string& message) {
    if (has_error_)
        return;
    has_error_ = true;
    error_ = ParseError{code, message};
}

void JsonStreamParser::process_tokens() {

    auto begin_object = [&]() {
        handler_({JsonEventType::StartObject, {}});
        stack_.push_back({ContainerType::Object, Expectation::KeyOrEnd});
    };

    auto begin_array = [&]() {
        handler_({JsonEventType::StartArray, {}});
        stack_.push_back({ContainerType::Array, Expectation::ValueOrEnd});
    };

    auto end_object = [&]() {
        handler_({JsonEventType::EndObject, {}});
        stack_.pop_back();
        if (stack_.empty())
            root_complete_ = true;
    };

    auto end_array = [&]() {
        handler_({JsonEventType::EndArray, {}});
        stack_.pop_back();
        if (stack_.empty())
            root_complete_ = true;
    };

    auto handle_value_token = [&](const Token& tok) -> bool {
        switch (tok.type) {
            case TokenType::LeftBrace:
                begin_object();
                return true;
            case TokenType::LeftBracket:
                begin_array();
                return true;
            case TokenType::String:
                handler_({JsonEventType::String, tok.value});
                if (stack_.empty())
                    root_complete_ = true;
                return true;
            case TokenType::Number:
                handler_({JsonEventType::Number, tok.value});
                if (stack_.empty())
                    root_complete_ = true;
                return true;
            case TokenType::True:
                handler_({JsonEventType::Boolean, "true"});
                if (stack_.empty())
                    root_complete_ = true;
                return true;
            case TokenType::False:
                handler_({JsonEventType::Boolean, "false"});
                if (stack_.empty())
                    root_complete_ = true;
                return true;
            case TokenType::Null:
                handler_({JsonEventType::Null, "null"});
                if (stack_.empty())
                    root_complete_ = true;
                return true;
            default:
                return false;
        }
    };

    while (!has_error_) {
        Token tok = tokenizer_.next_token();

        if (tok.type == TokenType::NeedMoreData)
            return;

        if (tok.type == TokenType::EndOfStream)
            return;

        if (tok.type == TokenType::Error) {
            set_error(tok.error_code == ErrorCode::None ? ErrorCode::UnexpectedToken : tok.error_code,
                      tok.error_message.empty() ? "Tokenizer error" : tok.error_message);
            return;
        }

        if (stack_.empty()) {
            if (root_complete_) {
                set_error(ErrorCode::UnexpectedToken, "Unexpected trailing token");
                return;
            }

            if (!handle_value_token(tok)) {
                set_error(ErrorCode::UnexpectedToken, "Unexpected token" );
                return;
            }

            continue;
        }

        ContainerState& state = stack_.back();

        if (state.type == ContainerType::Object) {
            switch (state.expect) {
                case Expectation::KeyOrEnd:
                    if (tok.type == TokenType::RightBrace) {
                        end_object();
                        break;
                    }
                    if (tok.type == TokenType::String) {
                        handler_({JsonEventType::Key, tok.value});
                        state.expect = Expectation::Colon;
                        break;
                    }
                    set_error(ErrorCode::UnexpectedToken, "Expected string key or '}'");
                    return;

                case Expectation::Colon:
                    if (tok.type == TokenType::Colon) {
                        state.expect = Expectation::Value;
                        break;
                    }
                    set_error(ErrorCode::UnexpectedToken, "Expected ':' after key");
                    return;

                case Expectation::Value:
                    if (!handle_value_token(tok)) {
                        set_error(ErrorCode::UnexpectedToken, "Expected value");
                        return;
                    }
                    state.expect = Expectation::CommaOrEnd;
                    break;

                case Expectation::CommaOrEnd:
                    if (tok.type == TokenType::Comma) {
                        state.expect = Expectation::KeyOrEnd;
                        break;
                    }
                    if (tok.type == TokenType::RightBrace) {
                        end_object();
                        break;
                    }
                    set_error(ErrorCode::UnexpectedToken, "Expected ',' or '}'");
                    return;

                case Expectation::ValueOrEnd:
                    set_error(ErrorCode::UnexpectedToken, "Invalid object state");
                    return;
            }
            continue;
        }

        // Array
        switch (state.expect) {
            case Expectation::ValueOrEnd:
                if (tok.type == TokenType::RightBracket) {
                    end_array();
                    break;
                }
                if (!handle_value_token(tok)) {
                    set_error(ErrorCode::UnexpectedToken, "Expected value or ']'");
                    return;
                }
                state.expect = Expectation::CommaOrEnd;
                break;

            case Expectation::CommaOrEnd:
                if (tok.type == TokenType::Comma) {
                    state.expect = Expectation::ValueOrEnd;
                    break;
                }
                if (tok.type == TokenType::RightBracket) {
                    end_array();
                    break;
                }
                set_error(ErrorCode::UnexpectedToken, "Expected ',' or ']'" );
                return;

            case Expectation::KeyOrEnd:
            case Expectation::Colon:
            case Expectation::Value:
                set_error(ErrorCode::UnexpectedToken, "Invalid array state");
                return;
        }
    }
}

} // namespace json
