#include "includes/sql/sql.h"
#include "includes/storage/storage_v2.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

Bytes read_bytes(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    input.seekg(0);
    Bytes bytes(static_cast<std::size_t>(length));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return bytes;
}

void write_slot(std::ofstream& output, const std::string& value) {
    if (value.size() > 100) throw std::runtime_error("fixture value exceeds v1 slot");
    std::array<char, 100> slot{};
    std::copy(value.begin(), value.end(), slot.begin());
    output.write(slot.data(), static_cast<std::streamsize>(slot.size()));
}

void write_v1(
    const std::string& table,
    const std::vector<std::string>& fields,
    const std::vector<std::vector<std::string>>& rows
) {
    std::ofstream schema(table + "_fields.bin", std::ios::binary | std::ios::trunc);
    write_slot(schema, std::to_string(fields.size()));
    for (const auto& field : fields) write_slot(schema, field);
    schema.close();
    std::ofstream data(table + ".bin", std::ios::binary | std::ios::trunc);
    for (const auto& row : rows) {
        for (const auto& value : row) write_slot(data, value);
    }
}

int run_migration(
    const std::string& source,
    const std::string& destination,
    bool dry_run,
    const std::string& crash_point = ""
) {
    const auto process = ::fork();
    if (process < 0) throw std::runtime_error("fork failed");
    if (process == 0) {
        if (!crash_point.empty()) {
            ::setenv("SQL_MIGRATION_CRASH_AT", crash_point.c_str(), 1);
        }
        if (dry_run) {
            ::execl(
                MIGRATION_TOOL_PATH,
                MIGRATION_TOOL_PATH,
                "--dry-run",
                "--destination",
                destination.c_str(),
                source.c_str(),
                nullptr
            );
        } else {
            ::execl(
                MIGRATION_TOOL_PATH,
                MIGRATION_TOOL_PATH,
                "--destination",
                destination.c_str(),
                source.c_str(),
                nullptr
            );
        }
        ::_exit(127);
    }
    int status = 0;
    if (::waitpid(process, &status, 0) != process || !WIFEXITED(status)) return -1;
    return WEXITSTATUS(status);
}

void cleanup(const std::string& table, const std::string& destination) {
    std::filesystem::remove(table + ".bin");
    std::filesystem::remove(table + "_fields.bin");
    std::filesystem::remove(destination);
    std::filesystem::remove(storage_v2::wal_path(destination));
    const auto prefix = std::filesystem::path(destination).filename().string() + ".tmp.";
    for (const auto& entry : std::filesystem::directory_iterator(".")) {
        const auto filename = entry.path().filename().string();
        if (filename.rfind(prefix, 0) == 0) std::filesystem::remove(entry.path());
    }
}

void test_copy_first_migration_and_queries() {
    const std::string source = "legacy_values";
    const std::string destination = "migrated_values.sql2";
    cleanup(source, destination);
    const std::vector<std::string> fields{"value", "category"};
    const std::vector<std::vector<std::string>> rows{
        {"", "empty"},
        {std::string(100, 'x'), "maximum"},
        {"10", "lexical"},
        {"2", "lexical"},
        {"2", "duplicate"},
    };
    write_v1(source, fields, rows);
    const auto source_data = read_bytes(source + ".bin");
    const auto source_schema = read_bytes(source + "_fields.bin");

    expect(run_migration(source, destination, true) == 0, "migration dry run should succeed");
    expect(!std::filesystem::exists(destination), "dry run must not create a destination");
    expect(read_bytes(source + ".bin") == source_data, "dry run must preserve v1 rows");
    expect(read_bytes(source + "_fields.bin") == source_schema, "dry run must preserve v1 schema");

    expect(run_migration(source, destination, false) == 0, "migration should succeed");
    const auto migrated = storage_v2::open_table(destination);
    expect(migrated.fields == fields, "migrated fields should match v1");
    expect(migrated.rows == rows, "migrated rows should match v1 byte-for-byte");
    expect(read_bytes(source + ".bin") == source_data, "migration must preserve v1 rows");
    expect(read_bytes(source + "_fields.bin") == source_schema, "migration must preserve v1 schema");
    expect(run_migration(source, destination, false) != 0, "existing destination should be rejected");

    std::filesystem::rename(destination, storage_v2::database_path(source));
    SQL sql;
    const auto equal = sql.command("select * from legacy_values where value = 2");
    expect(!sql.is_error(), "migrated equality query should succeed");
    expect(equal.select_recnos() == std::vector<long>({3, 4}), "duplicate equality query should match");
    const auto range = sql.command("select * from legacy_values where value < 2");
    expect(!sql.is_error(), "migrated range query should succeed");
    expect(range.select_recnos() == std::vector<long>({0, 2}), "lexical range should match v1 semantics");
    std::filesystem::rename(storage_v2::database_path(source), destination);
    cleanup(source, destination);
}

void test_maximum_v1_row_and_read_only_runtime() {
    const std::string source = "legacy_maximum";
    const std::string destination = "migrated_maximum.sql2";
    cleanup(source, destination);
    std::vector<std::string> fields;
    std::vector<std::string> row;
    for (int field = 0; field < 64; ++field) {
        fields.push_back("f" + std::to_string(field));
        row.push_back(std::string(100, static_cast<char>('A' + field % 26)));
    }
    write_v1(source, fields, {row});
    expect(run_migration(source, destination, false) == 0, "maximum-width v1 row should migrate");
    expect(storage_v2::open_table(destination).rows == std::vector<std::vector<std::string>>({row}),
           "maximum-width row should remain equivalent");

    bool read_only_rejected = false;
    try {
        Table legacy(source);
        legacy.insert_into(row);
    } catch (const std::runtime_error& error) {
        read_only_rejected = std::string(error.what()).find("read-only") != std::string::npos;
    }
    expect(read_only_rejected, "v1 runtime inserts should explain migration requirement");
    cleanup(source, destination);
}

void test_atomic_promotion_crash_points() {
    const std::string source = "legacy_promotion";
    const std::string destination = "migrated_promotion.sql2";
    cleanup(source, destination);
    write_v1(source, {"value"}, {{"safe"}});
    const auto source_data = read_bytes(source + ".bin");
    const auto source_schema = read_bytes(source + "_fields.bin");

    expect(run_migration(source, destination, false, "before-promotion") == 86,
           "migration should stop before promotion");
    expect(!std::filesystem::exists(destination), "pre-rename crash must not expose destination");
    expect(read_bytes(source + ".bin") == source_data, "pre-rename crash must preserve source rows");
    expect(read_bytes(source + "_fields.bin") == source_schema,
           "pre-rename crash must preserve source schema");
    cleanup("unused", destination);

    expect(run_migration(source, destination, false, "after-promotion") == 86,
           "migration should stop after atomic promotion");
    expect(storage_v2::open_table(destination).rows ==
               std::vector<std::vector<std::string>>({{"safe"}}),
           "post-rename crash must expose only a complete verified destination");
    expect(read_bytes(source + ".bin") == source_data, "post-rename crash must preserve source rows");
    expect(read_bytes(source + "_fields.bin") == source_schema,
           "post-rename crash must preserve source schema");
    cleanup(source, destination);
}

} // namespace

int main() {
    try {
        test_copy_first_migration_and_queries();
        std::cout << "PASS: dry-run, copy-first migration, and query equivalence\n";
        test_maximum_v1_row_and_read_only_runtime();
        std::cout << "PASS: maximum-width v1 migration and read-only compatibility\n";
        test_atomic_promotion_crash_points();
        std::cout << "PASS: atomic migration promotion crash points\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
