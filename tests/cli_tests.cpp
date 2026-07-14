#include "includes/storage/storage_v2.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<std::uint8_t> read_bytes(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    input.seekg(0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return bytes;
}

std::pair<int, std::string> run_inspection(const std::string& table) {
    std::array<int, 2> descriptors{};
    if (::pipe(descriptors.data()) != 0) throw std::runtime_error("pipe failed");
    const auto process = ::fork();
    if (process < 0) throw std::runtime_error("fork failed");
    if (process == 0) {
        ::close(descriptors[0]);
        ::dup2(descriptors[1], STDOUT_FILENO);
        ::dup2(descriptors[1], STDERR_FILENO);
        ::close(descriptors[1]);
        ::execl(
            RUN_SQL_PATH,
            RUN_SQL_PATH,
            "--inspect-storage",
            table.c_str(),
            nullptr
        );
        ::_exit(127);
    }
    ::close(descriptors[1]);
    std::string output;
    std::array<char, 1024> buffer{};
    while (true) {
        const auto count = ::read(descriptors[0], buffer.data(), buffer.size());
        if (count <= 0) break;
        output.append(buffer.data(), static_cast<std::size_t>(count));
    }
    ::close(descriptors[0]);
    int status = 0;
    if (::waitpid(process, &status, 0) != process || !WIFEXITED(status)) return {-1, output};
    return {WEXITSTATUS(status), output};
}

} // namespace

int main() {
    const std::string table = "cli_inspect";
    const auto path = storage_v2::database_path(table);
    std::filesystem::remove(path);
    std::filesystem::remove(storage_v2::wal_path(path));
    try {
        storage_v2::create_from_rows(path, {"name", "group"}, {{"Ada", "math"}});
        const auto before = read_bytes(path);
        const auto valid = run_inspection(table);
        expect(valid.first == 0, "valid storage inspection should exit zero");
        expect(valid.second.find("storage-format: 2") != std::string::npos,
               "inspection should report format version");
        expect(valid.second.find("row-count: 1") != std::string::npos,
               "inspection should report row count");
        expect(valid.second.find("index-roots:") != std::string::npos,
               "inspection should report persistent roots");
        expect(read_bytes(path) == before, "inspection CLI must not mutate the database");

        auto corrupt = before;
        corrupt[storage_v2::PAGE_SIZE + 40] ^= 0x4bU;
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(
                reinterpret_cast<const char*>(corrupt.data()),
                static_cast<std::streamsize>(corrupt.size())
            );
        }
        const auto invalid = run_inspection(table);
        expect(invalid.first == 1, "corrupt storage inspection should exit nonzero");
        expect(invalid.second.find("checksum") != std::string::npos,
               "corrupt storage inspection should explain checksum failure");
        std::filesystem::remove(path);
        std::filesystem::remove(storage_v2::wal_path(path));
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove(path);
        std::filesystem::remove(storage_v2::wal_path(path));
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
