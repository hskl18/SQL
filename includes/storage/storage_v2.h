#ifndef STORAGE_V2_H
#define STORAGE_V2_H

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace storage_v2 {

constexpr std::uint32_t PAGE_SIZE = 4096;
constexpr std::uint16_t FORMAT_VERSION = 2;

class CorruptionError : public std::runtime_error {
public:
    explicit CorruptionError(const std::string& message)
        : std::runtime_error("Storage corruption: " + message) {}
};

struct Inspection {
    std::uint16_t format_version = FORMAT_VERSION;
    std::uint64_t generation = 0;
    std::uint32_t page_count = 0;
    std::uint64_t row_count = 0;
    std::uint32_t field_count = 0;
    std::vector<std::uint32_t> index_roots;
    bool wal_present = false;
};

struct LoadedTable {
    std::vector<std::string> fields;
    std::vector<std::vector<std::string>> rows;
    std::vector<std::vector<std::pair<std::string, std::uint64_t>>> indexes;
    Inspection inspection;
};

std::string database_path(const std::string& table_name);
std::string wal_path(const std::string& database_path);
bool exists(const std::string& path);

void create_table(
    const std::string& path,
    const std::vector<std::string>& fields
);

void create_from_rows(
    const std::string& path,
    const std::vector<std::string>& fields,
    const std::vector<std::vector<std::string>>& rows
);

void initialize_owned_empty_file(
    const std::string& path,
    const std::vector<std::string>& fields,
    const std::vector<std::vector<std::string>>& rows
);

LoadedTable open_table(const std::string& path);

void insert_row(
    const std::string& path,
    const std::vector<std::string>& row
);

std::vector<std::uint64_t> query_index(
    const std::string& path,
    std::size_t field_index,
    const std::string& operation,
    const std::string& value
);

Inspection inspect(const std::string& path);
std::string describe(const std::string& path);

} // namespace storage_v2

#endif
