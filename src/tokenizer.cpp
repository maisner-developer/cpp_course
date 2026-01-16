#include "../include/json/tokenizer.hpp"
#include <cctype>
#include <stdexcept>

namespace json {

    Tokenizer::Tokenizer() = default;

    void Tokenizer::push_chunk(std::string_view chunk) {
        buffer_ += (chunk);
    }

    void Tokenizer::end_input() {
        input_finished_ = true;
    }

    void Tokenizer::skip_whitespace_() {
        size_t i = 0;
        while (i < buffer_.size() && std::isspace(static_cast<unsigned char>(buffer_[i]))) {
            ++i;
        }
        buffer_.erase(0, i);
    }

    Token Tokenizer::peek_token() {
        if (!has_peeked) {
            peeked = next_token();
            has_peeked = true;
        }
        return peeked;
    }

    Token Tokenizer::next_token() {
        if (has_peeked) {
            has_peeked = false;
            return peeked;
        }
        return really_read_next_token_();
    }

    Token Tokenizer::really_read_next_token_() {
        skip_whitespace_();
        if (buffer_.empty()) {
            if (input_finished_)
                return {TokenType::EndOfStream, "", ""};
            else
                return {TokenType::NeedMoreData, "", ""};
        }

        char c = buffer_[0];

        switch (c) {
            case '{': buffer_.erase(0,1); return {TokenType::LeftBrace, "", ""};
            case '}': buffer_.erase(0,1); return {TokenType::RightBrace, "", ""};
            case '[': buffer_.erase(0,1); return {TokenType::LeftBracket, "", ""};
            case ']': buffer_.erase(0,1); return {TokenType::RightBracket, "", ""};
            case ':': buffer_.erase(0,1); return {TokenType::Colon, "", ""};
            case ',': buffer_.erase(0,1); return {TokenType::Comma, "", ""};
            case '\"': return parse_string_();
            default:
                break;
        }

        if (c == '-' || std::isdigit(static_cast<unsigned char>(c)))
            return parse_number_();

        if (buffer_.rfind("true", 0) == 0)
            return parse_literal_("true", TokenType::True);

        if (buffer_.rfind("false", 0) == 0)
            return parse_literal_("false", TokenType::False);

        if (buffer_.rfind("null", 0) == 0)
            return parse_literal_("null", TokenType::Null);
        return {TokenType::Error, "", "unexpected character", ErrorCode::UnexpectedToken};
    }

    Token Tokenizer::parse_literal_(const std::string& expected, TokenType type) {
        if (buffer_.size() < expected.size()) {
            if (input_finished_)
                return {TokenType::Error, "", "unexpected end of input in literal", ErrorCode::UnexpectedEnd};
            else
                return {TokenType::NeedMoreData, "", ""};
        }

        if (buffer_.compare(0, expected.size(), expected) != 0)
            return {TokenType::Error, "", "invalid literal", ErrorCode::UnexpectedToken};

        buffer_.erase(0, expected.size());
        return {type, "", ""};
    }

    Token Tokenizer::parse_string_() {
        std::string result;
        size_t i = 1; // skip opening quote

        auto hex_value = [](char ch) -> int {
            if (ch >= '0' && ch <= '9') return ch - '0';
            if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
            if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
            return -1;
        };

        auto parse_hex4 = [&](size_t pos, uint16_t& out, bool& need_more) -> bool {
            if (pos + 4 > buffer_.size()) {
                need_more = true;
                return false;
            }

            need_more = false;
            uint16_t v = 0;
            for (size_t k = 0; k < 4; ++k) {
                int hv = hex_value(buffer_[pos + k]);
                if (hv < 0) return false;
                v = static_cast<uint16_t>((v << 4) | hv);
            }
            out = v;
            return true;
        };

        auto append_utf8 = [&](uint32_t cp) {
            if (cp <= 0x7F) {
                result.push_back(static_cast<char>(cp));
            } else if (cp <= 0x7FF) {
                result.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else if (cp <= 0xFFFF) {
                result.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else {
                result.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                result.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        };

        while (true) {
            if (i >= buffer_.size()) {
                if (input_finished_)
                    return {TokenType::Error, "", "unterminated string", ErrorCode::InvalidString};
                else
                    return {TokenType::NeedMoreData, "", ""};
            }

            char c = buffer_[i];

            if (c == '"') {
                buffer_.erase(0, i + 1);
                return {TokenType::String, result, ""};
            }

            if (c == '\\') {
                if (i + 1 >= buffer_.size()) {
                    return {TokenType::NeedMoreData, "", ""};
                }

                char esc = buffer_[i + 1];
                switch (esc) {
                    case '"': result.push_back('"'); break;
                    case '\\': result.push_back('\\'); break;
                    case '/': result.push_back('/'); break;
                    case 'n': result.push_back('\n'); break;
                    case 't': result.push_back('\t'); break;
                    case 'r': result.push_back('\r'); break;
                    case 'b': result.push_back('\b'); break;
                    case 'f': result.push_back('\f'); break;
                    case 'u': {
                        uint16_t cu1 = 0;
                        bool need_more = false;
                        if (!parse_hex4(i + 2, cu1, need_more)) {
                            if (need_more && !input_finished_)
                                return {TokenType::NeedMoreData, "", ""};
                            return {TokenType::Error, "", "invalid unicode escape", ErrorCode::InvalidUnicode};
                        }

                        uint32_t cp = cu1;

                        if (cu1 >= 0xD800 && cu1 <= 0xDBFF) {
                            size_t next = i + 6;
                            if (next + 2 > buffer_.size()) {
                                if (!input_finished_)
                                    return {TokenType::NeedMoreData, "", ""};
                                return {TokenType::Error, "", "invalid unicode escape", ErrorCode::InvalidUnicode};
                            }
                            if (buffer_[next] != '\\' || buffer_[next + 1] != 'u')
                                return {TokenType::Error, "", "invalid unicode escape", ErrorCode::InvalidUnicode};

                            uint16_t cu2 = 0;
                            bool need_more2 = false;
                            if (!parse_hex4(next + 2, cu2, need_more2)) {
                                if (need_more2 && !input_finished_)
                                    return {TokenType::NeedMoreData, "", ""};
                                return {TokenType::Error, "", "invalid unicode escape", ErrorCode::InvalidUnicode};
                            }
                            if (cu2 < 0xDC00 || cu2 > 0xDFFF)
                                return {TokenType::Error, "", "invalid unicode escape", ErrorCode::InvalidUnicode};

                            cp = 0x10000 + (((cu1 - 0xD800) << 10) | (cu2 - 0xDC00));
                            i += 12;
                        } else {
                            if (cu1 >= 0xDC00 && cu1 <= 0xDFFF)
                                return {TokenType::Error, "", "invalid unicode escape", ErrorCode::InvalidUnicode};
                            i += 6;
                        }

                        if (cp > 0x10FFFF)
                            return {TokenType::Error, "", "invalid unicode escape", ErrorCode::InvalidUnicode};

                        append_utf8(cp);
                        continue;
                    }
                    default:
                        return {TokenType::Error, "", "invalid escape", ErrorCode::InvalidEscape};
                }

                i += 2;
                continue;
            }

            // В JSON нельзя использовать управляющие символы внутри строк
            if (static_cast<unsigned char>(c) < 0x20) {
                return {TokenType::Error, "", "invalid string", ErrorCode::InvalidString};
            }
            result.push_back(c);
            ++i;
        }
    }

    Token Tokenizer::parse_number_() {
        size_t i = 0;
        const size_t n = buffer_.size();

        auto need_more = [&]() {
            return !input_finished_;
        };

        // Необязательный ведущий минус
        if (i < n && buffer_[i] == '-') {
            ++i;
        }

        if (i >= n) {
            return need_more() ? Token{TokenType::NeedMoreData, "", ""}
                               : Token{TokenType::Error, "", "invalid number", ErrorCode::InvalidNumber};
        }

        // Целая часть: либо одиночный '0', либо ненулевой разряд + последующие цифры
        if (buffer_[i] == '0') {
            ++i;
            if (i < n && std::isdigit(static_cast<unsigned char>(buffer_[i]))) {
                return {TokenType::Error, "", "invalid number", ErrorCode::InvalidNumber};
            }
        } else if (std::isdigit(static_cast<unsigned char>(buffer_[i]))) {
            while (i < n && std::isdigit(static_cast<unsigned char>(buffer_[i]))) {
                ++i;
            }
        } else {
            return {TokenType::Error, "", "invalid number", ErrorCode::InvalidNumber};
        }

        // Дробная часть: '.' и как минимум одна цифра
        if (i < n && buffer_[i] == '.') {
            ++i;
            if (i >= n) {
                return need_more() ? Token{TokenType::NeedMoreData, "", ""}
                                   : Token{TokenType::Error, "", "invalid number", ErrorCode::InvalidNumber};
            }
            if (!std::isdigit(static_cast<unsigned char>(buffer_[i]))) {
                return {TokenType::Error, "", "invalid number", ErrorCode::InvalidNumber};
            }
            while (i < n && std::isdigit(static_cast<unsigned char>(buffer_[i]))) {
                ++i;
            }
        }

        // Экспонента: 'e'|'E' с необязательным знаком и минимум одной цифрой
        if (i < n && (buffer_[i] == 'e' || buffer_[i] == 'E')) {
            ++i;
            if (i >= n) {
                return need_more() ? Token{TokenType::NeedMoreData, "", ""}
                                   : Token{TokenType::Error, "", "invalid number", ErrorCode::InvalidNumber};
            }
            if (buffer_[i] == '+' || buffer_[i] == '-') {
                ++i;
                if (i >= n) {
                    return need_more() ? Token{TokenType::NeedMoreData, "", ""}
                                       : Token{TokenType::Error, "", "invalid number", ErrorCode::InvalidNumber};
                }
            }
            if (!std::isdigit(static_cast<unsigned char>(buffer_[i]))) {
                return {TokenType::Error, "", "invalid number", ErrorCode::InvalidNumber};
            }
            while (i < n && std::isdigit(static_cast<unsigned char>(buffer_[i]))) {
                ++i;
            }
        }

        // Если буфер закончился на середине числа и вход еще не завершен — ждём данные
        if (i == n && !input_finished_)
            return {TokenType::NeedMoreData, "", ""};

        std::string num = buffer_.substr(0, i);
        buffer_.erase(0, i);
        return {TokenType::Number, num, ""};
    }
}