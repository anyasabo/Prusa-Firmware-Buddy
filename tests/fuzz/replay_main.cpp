// Runs LLVMFuzzerTestOneInput once per file named on the command line (directories are walked
// recursively). Used when BUDDY_FUZZ is off, so every harness builds with any compiler and its
// corpus replays under ctest as a regression test.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

namespace fs = std::filesystem;

static void run_file(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::fprintf(stderr, "Running: %s (%zu bytes)\n", path.string().c_str(), buf.size());
    LLVMFuzzerTestOneInput(buf.data(), buf.size());
}

int main(int argc, char **argv) {
    size_t count = 0;
    for (int i = 1; i < argc; ++i) {
        const fs::path p(argv[i]);
        if (!fs::exists(p)) {
            std::fprintf(stderr, "No such corpus path: %s\n", argv[i]);
            return 1;
        }
        if (fs::is_directory(p)) {
            for (const auto &entry : fs::recursive_directory_iterator(p)) {
                if (entry.is_regular_file() && entry.path().filename().string()[0] != '.') {
                    run_file(entry.path());
                    ++count;
                }
            }
        } else {
            run_file(p);
            ++count;
        }
    }
    std::fprintf(stderr, "Executed %zu inputs\n", count);
    return count == 0 ? 1 : 0; // an empty corpus is a misconfiguration, not a pass
}
