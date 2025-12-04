#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>

struct KeyValue {
    std::string key;
    int value;
};

std::string normalize_word(const std::string &w) {
    std::string res;
    res.reserve(w.size());
    for (unsigned char c : w) {
        if (std::isalnum(c)) {
            res.push_back(std::tolower(c));
        }
    }
    return res;
}

std::vector<KeyValue> map_line(const std::string &line) {
    std::vector<KeyValue> out;
    std::string current;

    for (unsigned char c : line) {
        if (std::isalnum(c)) {
            current.push_back(c);
        } else {
            if (!current.empty()) {
                std::string w = normalize_word(current);
                if (!w.empty()) {
                    out.push_back({w, 1});
                }
                current.clear();
            }
        }
    }
    if (!current.empty()) {
        std::string w = normalize_word(current);
        if (!w.empty()) {
            out.push_back({w, 1});
        }
    }

    return out;
}

std::vector<KeyValue> reduce_all(std::vector<KeyValue> &intermediate) {
    std::vector<KeyValue> result;
    if (intermediate.empty()) return result;

    std::sort(intermediate.begin(), intermediate.end(),
              [](const KeyValue &a, const KeyValue &b) {
                  return a.key < b.key;
              });

    std::string current_key = intermediate[0].key;
    int current_sum = 0;

    for (const auto &kv : intermediate) {
        if (kv.key == current_key) {
            current_sum += kv.value;
        } else {
            result.push_back({current_key, current_sum});
            current_key = kv.key;
            current_sum = kv.value;
        }
    }
    result.push_back({current_key, current_sum});

    return result;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0]
                  << " <output_file> <input_file1> [input_file2 ...]\n";
        return 1;
    }

    std::string output_file = argv[1];

    std::vector<KeyValue> intermediate;

    for (int i = 2; i < argc; ++i) {
        std::string input_file = argv[i];
        std::ifstream in(input_file);
        if (!in) {
            std::cerr << "Error: cannot open input file: "
                      << input_file << "\n";
            continue; 
        }

        std::string line;
        while (std::getline(in, line)) {
            auto kvs = map_line(line);
            intermediate.insert(intermediate.end(), kvs.begin(), kvs.end());
        }
        in.close();
    }

    std::cout << "[MapReduce] Mapped " << intermediate.size()
              << " key-value pairs.\n";

    auto result = reduce_all(intermediate);
    std::cout << "[MapReduce] Reduced to " << result.size()
              << " unique words.\n";

    std::ofstream out(output_file);
    if (!out) {
        std::cerr << "Error: cannot open output file: "
                  << output_file << "\n";
        return 1;
    }

    for (const auto &kv : result) {
        out << kv.key << " " << kv.value << "\n";
    }
    out.close();

    std::cout << "[MapReduce] Result written to: " << output_file << "\n";
    return 0;
}
