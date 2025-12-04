#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>

struct LengthPath {
    int length;
    std::string path;
};

LengthPath map_line(const std::string &line) {
    LengthPath lp;
    if (line.empty()) {
        lp.length = -1;
        lp.path = "";
    } else {
        lp.length = static_cast<int>(line.size());
        lp.path = line;
    }
    return lp;
}

std::vector<LengthPath> reduce_all(std::vector<LengthPath> &intermediate) {
    std::vector<LengthPath> result;
    if (intermediate.empty()) return result;

    intermediate.erase(
        std::remove_if(intermediate.begin(), intermediate.end(),
                       [](const LengthPath &lp){ return lp.length < 0; }),
        intermediate.end()
    );
    if (intermediate.empty()) return result;

    std::sort(intermediate.begin(), intermediate.end(),
              [](const LengthPath &a, const LengthPath &b) {
                  if (a.length != b.length) return a.length > b.length;
                  return a.path < b.path; 
              });

    int maxLen = intermediate.front().length;
    for (const auto &lp : intermediate) {
        if (lp.length == maxLen) {
            result.push_back(lp);
        } else {
            break; 
        }
    }
    return result;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0]
                  << " <output_file> <input_file1> [input_file2 ...]\n";
        return 1;
    }

    std::string output_file = argv[1];

    std::vector<LengthPath> intermediate;

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
            LengthPath lp = map_line(line);
            if (lp.length >= 0) {
                intermediate.push_back(lp);
            }
        }
        in.close();
    }

    std::cout << "[MapReduce] Mapped " << intermediate.size()
              << " path entries.\n";

    auto result = reduce_all(intermediate);

    if (result.empty()) {
        std::cerr << "[MapReduce] No valid paths found.\n";
        return 1;
    }

    std::cout << "[MapReduce] Longest length = "
              << result.front().length
              << ", number of longest paths = "
              << result.size() << "\n";

    std::ofstream out(output_file);
    if (!out) {
        std::cerr << "Error: cannot open output file: "
                  << output_file << "\n";
        return 1;
    }

    for (const auto &lp : result) {
        out << lp.length << " " << lp.path << "\n";
    }
    out.close();

    std::cout << "[MapReduce] Result written to: "
              << output_file << "\n";
    return 0;
}
