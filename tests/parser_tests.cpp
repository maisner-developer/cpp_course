#include "../include/json/parser.hpp"
#include "../include/json/value.hpp"
#include "../include/json/schema.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <atomic>

using json::ParseError;
using json::ParseLimits;
using json::ParseResult;
using json::Value;

static const char* error_code_to_string(json::ErrorCode code) {
    switch (code) {
        case json::ErrorCode::None: return "None";
        case json::ErrorCode::UnexpectedEnd: return "UnexpectedEnd";
        case json::ErrorCode::UnexpectedToken: return "UnexpectedToken";
        case json::ErrorCode::InvalidNumber: return "InvalidNumber";
        case json::ErrorCode::InvalidString: return "InvalidString";
        case json::ErrorCode::InvalidEscape: return "InvalidEscape";
        case json::ErrorCode::InvalidUnicode: return "InvalidUnicode";
        case json::ErrorCode::DepthLimitExceeded: return "DepthLimitExceeded";
        case json::ErrorCode::NodeLimitExceeded: return "NodeLimitExceeded";
        case json::ErrorCode::KeyTooLong: return "KeyTooLong";
        case json::ErrorCode::StringTooLong: return "StringTooLong";
        case json::ErrorCode::DuplicateKey: return "DuplicateKey";
        default: return "Unknown";
    }
}

static bool is_error(const ParseResult& r, json::ErrorCode code) {
    if (!std::holds_alternative<ParseError>(r))
        return false;
    const auto& e = std::get<ParseError>(r);
    return e.code == code;
}

static Value require_value(const ParseResult& r) {
    assert(std::holds_alternative<Value>(r));
    return std::get<Value>(r);
}

static bool read_file(const std::filesystem::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::ostringstream oss;
    oss << in.rdbuf();
    out = oss.str();
    return true;
}

static void test_basic_object() {
    auto r = json::parse_json("{\"a\":1,\"b\":true,\"c\":null}");
    Value v = require_value(r);
    assert(v["a"].get<double>() == 1.0);
    assert(v["b"].get<bool>() == true);
    assert(v["c"].is_null());
}

static void test_array_and_string_escape() {
    auto r = json::parse_json("[\"x\\n\",\"y\\t\",\"\\u0041\"]");
    Value v = require_value(r);
    assert(v[0].get<std::string>() == "x\n");
    assert(v[1].get<std::string>() == "y\t");
    assert(v[2].get<std::string>() == "A");
}

static void test_trailing_tokens() {
    auto r = json::parse_json("true false");
    assert(is_error(r, json::ErrorCode::UnexpectedToken));
}

static void test_invalid_number() {
    auto r = json::parse_json("01");
    assert(is_error(r, json::ErrorCode::InvalidNumber));
}

static void test_limits_depth() {
    ParseLimits limits;
    limits.max_depth = 1;
    auto r = json::parse_json("[[1]]", limits);
    assert(is_error(r, json::ErrorCode::DepthLimitExceeded));
}

static void test_limits_key_length() {
    ParseLimits limits;
    limits.max_key_length = 3;
    auto r = json::parse_json("{\"abcd\":1}", limits);
    assert(is_error(r, json::ErrorCode::KeyTooLong));
}

static void test_limits_string_length() {
    ParseLimits limits;
    limits.max_string_length = 2;
    auto r = json::parse_json("\"abcd\"", limits);
    assert(is_error(r, json::ErrorCode::StringTooLong));
}

static void test_duplicate_keys() {
    ParseLimits limits;
    limits.forbid_duplicate_keys = true;
    auto r = json::parse_json("{\"a\":1,\"a\":2}", limits);
    assert(is_error(r, json::ErrorCode::DuplicateKey));
}

static void test_unexpected_end() {
    auto r = json::parse_json("{\"a\":1");
    assert(is_error(r, json::ErrorCode::UnexpectedEnd));
}

static void test_schema_type_required_properties_items_enum() {
    Value schema = require_value(json::parse_json(
        "{"
        "  \"type\": \"object\","
        "  \"required\": [\"id\", \"tags\"],"
        "  \"properties\": {"
        "    \"id\": { \"type\": \"number\" },"
        "    \"tags\": { \"type\": \"array\", \"items\": { \"type\": \"string\" } },"
        "    \"kind\": { \"enum\": [\"a\", \"b\"] }"
        "  }"
        "}"
    ));

    Value ok = require_value(json::parse_json(
        "{\"id\":1,\"tags\":[\"x\",\"y\"],\"kind\":\"a\"}"
    ));

    auto okRes = json::validate_schema(schema, ok);
    assert(std::holds_alternative<std::monostate>(okRes));

    Value badType = require_value(json::parse_json(
        "{\"id\":\"nope\",\"tags\":[\"x\"],\"kind\":\"a\"}"
    ));
    auto badTypeRes = json::validate_schema(schema, badType);
    assert(std::holds_alternative<json::SchemaError>(badTypeRes));

    Value badEnum = require_value(json::parse_json(
        "{\"id\":1,\"tags\":[\"x\"],\"kind\":\"c\"}"
    ));
    auto badEnumRes = json::validate_schema(schema, badEnum);
    assert(std::holds_alternative<json::SchemaError>(badEnumRes));

    Value badItems = require_value(json::parse_json(
        "{\"id\":1,\"tags\":[1,2],\"kind\":\"a\"}"
    ));
    auto badItemsRes = json::validate_schema(schema, badItems);
    assert(std::holds_alternative<json::SchemaError>(badItemsRes));
}

static void test_schema_constraints_stage2() {
    Value schema = require_value(json::parse_json(
        "{"
        "  \"type\": \"object\","
        "  \"properties\": {"
        "    \"age\": { \"type\": \"number\", \"minimum\": 18, \"maximum\": 60 },"
        "    \"name\": { \"type\": \"string\", \"minLength\": 2, \"maxLength\": 5 },"
        "    \"tags\": { \"type\": \"array\", \"minItems\": 1, \"maxItems\": 3 },"
        "    \"obj\": { \"type\": \"object\", \"minProperties\": 1, \"maxProperties\": 2 }"
        "  }"
        "}"
    ));

    Value ok = require_value(json::parse_json(
        "{\"age\":30,\"name\":\"Ann\",\"tags\":[\"a\"],\"obj\":{\"x\":1}}"
    ));
    auto okRes = json::validate_schema(schema, ok);
    assert(std::holds_alternative<std::monostate>(okRes));

    Value badNum = require_value(json::parse_json(
        "{\"age\":10,\"name\":\"Ann\",\"tags\":[\"a\"],\"obj\":{\"x\":1}}"
    ));
    auto badNumRes = json::validate_schema(schema, badNum);
    assert(std::holds_alternative<json::SchemaError>(badNumRes));

    Value badStr = require_value(json::parse_json(
        "{\"age\":30,\"name\":\"A\",\"tags\":[\"a\"],\"obj\":{\"x\":1}}"
    ));
    auto badStrRes = json::validate_schema(schema, badStr);
    assert(std::holds_alternative<json::SchemaError>(badStrRes));

    Value badArr = require_value(json::parse_json(
        "{\"age\":30,\"name\":\"Ann\",\"tags\":[\"a\",\"b\",\"c\",\"d\"],\"obj\":{\"x\":1}}"
    ));
    auto badArrRes = json::validate_schema(schema, badArr);
    assert(std::holds_alternative<json::SchemaError>(badArrRes));

    Value badObj = require_value(json::parse_json(
        "{\"age\":30,\"name\":\"Ann\",\"tags\":[\"a\"],\"obj\":{\"x\":1,\"y\":2,\"z\":3}}"
    ));
    auto badObjRes = json::validate_schema(schema, badObj);
    assert(std::holds_alternative<json::SchemaError>(badObjRes));
}

static void test_multithreaded_parsing() {
    const std::vector<std::string> inputs = {
        "{\"a\":1}",
        "[1,2,3]",
        "\"text\"",
        "true",
        "{\"x\":[{\"y\":2}]}"
    };

    std::atomic<int> okCount{0};
    std::vector<std::thread> threads;
    threads.reserve(inputs.size());

    for (const auto& s : inputs) {
        threads.emplace_back([&]() {
            auto r = json::parse_json(s);
            if (std::holds_alternative<Value>(r))
                okCount.fetch_add(1, std::memory_order_relaxed);
        });
    }

    for (auto& t : threads)
        t.join();

    assert(okCount.load(std::memory_order_relaxed) == static_cast<int>(inputs.size()));
}

static void test_large_dataset_parsing() {
    std::filesystem::path root = std::filesystem::path(__FILE__).parent_path().parent_path();
    std::filesystem::path dataset = root / "gen_dataset.json";

    assert(std::filesystem::exists(dataset));

    std::string content;
    bool ok = read_file(dataset, content);
    assert(ok);

    ParseLimits limits;
    limits.max_total_nodes = content.size();
    limits.max_string_length = content.size();
    limits.max_key_length = 1'000'000;
    limits.max_depth = 512;

    unsigned int threads = std::thread::hardware_concurrency();
    if (threads == 0) threads = 2;

    auto start = std::chrono::steady_clock::now();
    auto result = json::parse_json_parallel(content, threads, 64, limits);
    auto end = std::chrono::steady_clock::now();

    if (std::holds_alternative<ParseError>(result)) {
        const auto& err = std::get<ParseError>(result);
        std::cerr << "Parse error: " << error_code_to_string(err.code)
                  << " (code=" << static_cast<int>(err.code) << ")"
                  << ", message: " << err.message
                  << ", position: " << err.position << "\n";

        auto single = json::parse_json(content, limits);
        if (std::holds_alternative<ParseError>(single)) {
            const auto& err2 = std::get<ParseError>(single);
            std::cerr << "Single-thread parse error: " << error_code_to_string(err2.code)
                      << " (code=" << static_cast<int>(err2.code) << ")"
                      << ", message: " << err2.message
                      << ", position: " << err2.position << "\n";
        } else {
            std::cerr << "Single-thread parse succeeded; issue likely in parallel splitting.\n";
        }
        std::cerr.flush();
    }
    assert(std::holds_alternative<Value>(result));

    std::chrono::duration<double> elapsed = end - start;
    auto size_mb = static_cast<double>(content.size()) / (1024.0 * 1024.0);
    std::cout << "Dataset size: " << size_mb << " MB, parse time: " << elapsed.count() << " s\n";
}

int main() {
    test_basic_object();
    test_array_and_string_escape();
    test_trailing_tokens();
    test_invalid_number();
    test_limits_depth();
    test_limits_key_length();
    test_limits_string_length();
    test_duplicate_keys();
    test_unexpected_end();
    test_schema_type_required_properties_items_enum();
    test_schema_constraints_stage2();
    test_multithreaded_parsing();
    test_large_dataset_parsing();

    std::cout << "All tests passed\n";
    return 0;
}
