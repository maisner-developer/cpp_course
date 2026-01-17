#include "../include/json/parser.hpp"
#include "../include/json/value.hpp"
#include "../include/json/schema.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <atomic>

using json::ParseError;
using json::ParseLimits;
using json::ParseResult;
using json::Value;

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

    std::cout << "All tests passed\n";
    return 0;
}
