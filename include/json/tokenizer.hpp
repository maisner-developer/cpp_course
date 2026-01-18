/*
    json/tokenizer.hpp
    ----------------
    Declares the Tokenizer class for tokenizing JSON input.
*/

#pragma once

#include <string>
#include <optional>
#include <vector>
#include <string_view>
#include "error.hpp"

namespace json {

    // Available token types
    enum class TokenType {
        /*0*/   LeftBrace,      // {
        /*1*/   RightBrace,     // }
        /*2*/   LeftBracket,    // [
        /*3*/   RightBracket,   // ]
        /*4*/   Colon,          // :
        /*5*/   Comma,          // ,
        /*6*/   String,
        /*7*/   Number,
        /*8*/   True,
        /*9*/   False,
        /*10*/  Null,
        /*11*/  EndOfStream,
        /*12*/  NeedMoreData,
        /*13*/  Error
    };

    // Token structure
    struct Token {
        TokenType type;
        std::string value;          // for String and Number types
        std::string error_message;  // for Error type
        ErrorCode error_code = ErrorCode::None; // for Error type
    };

    // streaming JSON tokenizer
    class Tokenizer {
    public:
        Tokenizer();

        // add a chunk of input data
        void push_chunk(std::string_view chunk);

        // get the next token from the input
        Token next_token();

        // peek at the next token without consuming it
        Token peek_token();

        // true if no more input will be provided
        void end_input();

    private:
        std::string buffer_; // internal buffer
        size_t pos_ = 0;
        bool input_finished_ = false;
        bool has_peeked = false;
        Token peeked;

        // helpers
        void skip_whitespace_();
        void compact_buffer_();
        Token really_read_next_token_();
        Token parse_string_();
        Token parse_number_();
        Token parse_literal_(const std::string& expected, TokenType type);
    };

}
