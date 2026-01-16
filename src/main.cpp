#include "../include/json/parser.hpp"

#include <iostream>
#include <sstream>
#include <string>

int main() {

    std::ostringstream oss;
    oss << std::cin.rdbuf();
    std::string json_text = oss.str();

    if (json_text.empty()) {
        std::cout << "Input is empty\n";
        return 1;
    }

    auto result = json::parse_json(json_text);

    if (std::holds_alternative<json::ParseError>(result)) {
        const auto& err = std::get<json::ParseError>(result);
        std::cout << "Parse error: " << err.message << "\n";
        return 1;
    }

    const json::Value& root = std::get<json::Value>(result);
    std::cout << root << "\n";

    // json::JsonStreamParser parser([&](const json::JsonEvent& e){
    //     std::cout << "EVENT: ";

    //     switch (e.type) {
    //         case json::JsonEventType::StartObject: std::cout << "{\n"; break;
    //         case json::JsonEventType::EndObject: std::cout << "}\n"; break;
    //         case json::JsonEventType::StartArray: std::cout << "[\n"; break;
    //         case json::JsonEventType::EndArray: std::cout << "]\n"; break;
    //         case json::JsonEventType::String: std::cout << "String: " << e.value << "\n"; break;
    //         case json::JsonEventType::Number: std::cout << "Number: " << e.value << "\n"; break;
    //         default: std::cout << "Other\n"; break;
    //     }
    // });

    // parser.feed("{\"user\": {\"id\": 123, \"name\": \"Ива");
    // parser.feed("н\", \"active\": true, \"roles\": [\"admin\", ");
    // parser.feed("\"editor\", null], \"profile\": {\"age\": 27, \"height\": 1.82e0, \"lang");
    // parser.feed("uages\": []}} , \"items\": [ {\"id\": 1, \"tags\": [\"a\",\"b\"]}, ");
    // parser.feed("{\"id\": 2, \"tags\": [\"c\",\"d\",\"e\"]} ] }");
    // parser.finalize();


    return 0;
}
