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

struct ParallelOptions {
    bool parse_enabled = false;
    size_t threads = 0;
    size_t batch_size = 64;
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

static std::string serialize_value(const json::Value& v) {
    std::ostringstream oss;
    oss << v;
    return oss.str();
}


} // namespace

int main(int argc, char** argv) {
    ParallelOptions opts;
    opts.threads = std::thread::hardware_concurrency();
    if (opts.threads == 0) opts.threads = 2;

    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-j" || arg == "--threads") && i + 1 < argc) {
            opts.threads = static_cast<size_t>(std::max(1, std::stoi(argv[++i])));
            continue;
        }
        if (arg == "--parallel-parse") {
            opts.parse_enabled = true;
            continue;
        }
        if (arg == "--batch-size" && i + 1 < argc) {
            opts.batch_size = static_cast<size_t>(std::max(1, std::stoi(argv[++i])));
            continue;
        }
        files.push_back(arg);
    }

    // No files: read JSON from stdin
    if (files.empty()) {
        std::ostringstream oss;
        oss << std::cin.rdbuf();
        std::string json_text = oss.str();

        if (json_text.empty()) {
            std::cout << "Input is empty\n";
            return 1;
        }

        auto result = opts.parse_enabled
            ? json::parse_json_parallel(json_text, opts.threads, opts.batch_size)
            : json::parse_json(json_text);

        if (std::holds_alternative<json::ParseError>(result)) {
            const auto& err = std::get<json::ParseError>(result);
            std::cout << "Parse error: " << err.message << "\n";
            return 1;
        }

        const json::Value& root = std::get<json::Value>(result);
        std::cout << root << "\n";
        return 0;
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

            auto result = opts.parse_enabled
                ? json::parse_json_parallel(content, opts.threads, opts.batch_size)
                : json::parse_json(content);
            if (std::holds_alternative<json::ParseError>(result)) {
                const auto& err = std::get<json::ParseError>(result);
                results[idx].ok = false;
                results[idx].output = err.message;
                continue;
            }

            const json::Value& root = std::get<json::Value>(result);
            results[idx].ok = true;
            results[idx].output = serialize_value(root);
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(opts.threads);
    for (size_t i = 0; i < opts.threads; ++i)
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
