#include "../include/json/parser.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct ParseOutput {
    std::string path;
    bool ok = false;
    std::string output;
};

static bool read_file(const std::filesystem::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::ostringstream oss;
    oss << in.rdbuf();
    out = oss.str();
    return true;
}

} // namespace

int main(int argc, char** argv) {

    // No args: read JSON from stdin
    if (argc == 1) {
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
        return 0;
    }

    // Args: parse multiple files (optionally in parallel)
    size_t threads = std::thread::hardware_concurrency();
    if (threads == 0) threads = 2;

    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-j" || arg == "--threads") && i + 1 < argc) {
            threads = static_cast<size_t>(std::max(1, std::stoi(argv[++i])));
            continue;
        }
        files.push_back(arg);
    }

    if (files.empty()) {
        std::cout << "No input files provided\n";
        return 1;
    }

    std::queue<size_t> queue;
    for (size_t i = 0; i < files.size(); ++i)
        queue.push(i);

    std::mutex mtx;

    std::vector<ParseOutput> results(files.size());

    auto worker = [&]() {
        while (true) {
            size_t idx = 0;
            {
                std::lock_guard<std::mutex> lock(mtx);
                if (queue.empty())
                    return;
                idx = queue.front();
                queue.pop();
            }

            const auto& pathStr = files[idx];
            std::filesystem::path path(pathStr);
            results[idx].path = pathStr;

            std::string content;
            if (!read_file(path, content)) {
                results[idx].ok = false;
                results[idx].output = "Failed to read file";
                continue;
            }

            auto result = json::parse_json(content);
            if (std::holds_alternative<json::ParseError>(result)) {
                const auto& err = std::get<json::ParseError>(result);
                results[idx].ok = false;
                results[idx].output = err.message;
                continue;
            }

            const json::Value& root = std::get<json::Value>(result);
            std::ostringstream oss;
            oss << root;
            results[idx].ok = true;
            results[idx].output = oss.str();
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(threads);
    for (size_t i = 0; i < threads; ++i)
        workers.emplace_back(worker);

    for (auto& t : workers)
        t.join();

    for (const auto& r : results) {
        if (r.ok) {
            std::cout << r.path << ": " << r.output << "\n";
        } else {
            std::cout << r.path << ": Parse error: " << r.output << "\n";
        }
    }

    return 0;
}
