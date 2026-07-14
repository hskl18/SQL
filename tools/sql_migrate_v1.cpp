#include "includes/storage/storage_v2.h"
#include "version.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

constexpr std::size_t SLOT_SIZE = 100;
constexpr std::size_t MAX_FIELDS = 64;

struct LegacyTable {
    std::vector<std::string> fields;
    std::vector<std::vector<std::string>> rows;
};

std::vector<std::uint8_t> read_bytes(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Unable to open v1 file read-only: " + path);
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    if (length < 0) throw std::runtime_error("Unable to determine v1 file size: " + path);
    input.seekg(0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
            throw std::runtime_error("Unable to read complete v1 file: " + path);
        }
    }
    return bytes;
}

std::string decode_slot(const std::uint8_t* slot, const std::string& context) {
    const auto end = std::find(slot, slot + SLOT_SIZE, 0);
    if (end != slot + SLOT_SIZE) {
        for (auto cursor = end; cursor != slot + SLOT_SIZE; ++cursor) {
            if (*cursor != 0) throw std::runtime_error(context + " has non-zero bytes after its terminator");
        }
    }
    return std::string(reinterpret_cast<const char*>(slot), reinterpret_cast<const char*>(end));
}

LegacyTable read_legacy(const std::string& source) {
    const auto schema_path = source + "_fields.bin";
    const auto rows_path = source + ".bin";
    const auto schema = read_bytes(schema_path);
    if (schema.size() < SLOT_SIZE || schema.size() % SLOT_SIZE != 0) {
        throw std::runtime_error("V1 field metadata size is invalid");
    }
    const auto count_text = decode_slot(schema.data(), "V1 field count");
    if (count_text.empty() || !std::all_of(count_text.begin(), count_text.end(), [](unsigned char value) {
        return std::isdigit(value) != 0;
    })) {
        throw std::runtime_error("V1 field count is invalid");
    }
    std::size_t parsed = 0;
    const auto raw_count = std::stoull(count_text, &parsed);
    if (parsed != count_text.size() || raw_count == 0 || raw_count > MAX_FIELDS) {
        throw std::runtime_error("V1 field count is invalid");
    }
    const auto field_count = static_cast<std::size_t>(raw_count);
    if (schema.size() != (field_count + 1) * SLOT_SIZE) {
        throw std::runtime_error("V1 field metadata size is invalid");
    }
    std::vector<std::string> fields;
    fields.reserve(field_count);
    std::set<std::string> unique;
    for (std::size_t field = 0; field < field_count; ++field) {
        auto name = decode_slot(schema.data() + (field + 1) * SLOT_SIZE, "V1 field name");
        if (name.empty() || !unique.insert(name).second) {
            throw std::runtime_error("V1 schema contains empty or duplicate fields");
        }
        fields.push_back(std::move(name));
    }

    const auto row_bytes = read_bytes(rows_path);
    if (field_count > std::numeric_limits<std::size_t>::max() / SLOT_SIZE) {
        throw std::runtime_error("V1 row width overflows this build");
    }
    const auto row_width = field_count * SLOT_SIZE;
    if (row_bytes.size() % row_width != 0) {
        throw std::runtime_error("V1 record file contains a partial row");
    }
    std::vector<std::vector<std::string>> rows;
    rows.reserve(row_bytes.size() / row_width);
    for (std::size_t offset = 0; offset < row_bytes.size(); offset += row_width) {
        std::vector<std::string> row;
        row.reserve(field_count);
        for (std::size_t field = 0; field < field_count; ++field) {
            row.push_back(decode_slot(
                row_bytes.data() + offset + field * SLOT_SIZE,
                "V1 row value"
            ));
        }
        rows.push_back(std::move(row));
    }
    return LegacyTable{std::move(fields), std::move(rows)};
}

void fsync_directory(const std::filesystem::path& path) {
    auto directory = path.parent_path();
    if (directory.empty()) directory = ".";
    const int descriptor = ::open(directory.c_str(), O_RDONLY);
    if (descriptor < 0) {
        throw std::runtime_error(
            "Unable to open destination directory: " + std::string(std::strerror(errno))
        );
    }
    if (::fsync(descriptor) != 0) {
        const auto message = std::string(std::strerror(errno));
        ::close(descriptor);
        throw std::runtime_error("Unable to fsync destination directory: " + message);
    }
    ::close(descriptor);
}

void print_usage() {
    std::cerr
        << "Usage: sql_migrate_v1 [--dry-run] [--destination <path>] <v1-table>\n";
}

void maybe_migration_crash(const std::string& point) {
    const char* configured = std::getenv("SQL_MIGRATION_CRASH_AT");
    if (configured != nullptr && point == configured) ::_exit(86);
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        bool dry_run = false;
        std::string destination;
        std::string source;
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--version") {
                std::cout << "sql_migrate_v1 " << SQL_IN_CPP_VERSION
                          << " (storage format " << storage_v2::FORMAT_VERSION << ")\n";
                return 0;
            }
            if (argument == "--dry-run") {
                dry_run = true;
            } else if (argument == "--destination") {
                if (++index >= argc) throw std::invalid_argument("--destination requires a path");
                destination = argv[index];
            } else if (!argument.empty() && argument.front() == '-') {
                throw std::invalid_argument("Unknown option: " + argument);
            } else if (source.empty()) {
                source = argument;
            } else {
                throw std::invalid_argument("Only one v1 table can be migrated per invocation");
            }
        }
        if (source.empty()) {
            print_usage();
            return 2;
        }
        if (destination.empty()) destination = storage_v2::database_path(source);

        const auto source_schema_path = source + "_fields.bin";
        const auto source_rows_path = source + ".bin";
        const auto source_schema_before = read_bytes(source_schema_path);
        const auto source_rows_before = read_bytes(source_rows_path);
        const auto legacy = read_legacy(source);
        std::cout << "source: " << source << '\n';
        std::cout << "destination: " << destination << '\n';
        std::cout << "fields: " << legacy.fields.size() << '\n';
        std::cout << "rows: " << legacy.rows.size() << '\n';
        if (dry_run) {
            std::cout << "result: dry-run verified, no files written\n";
            return 0;
        }
        if (std::filesystem::exists(destination) ||
            std::filesystem::exists(storage_v2::wal_path(destination))) {
            throw std::runtime_error("Destination already exists: " + destination);
        }

        std::string temporary_template = destination + ".tmp.XXXXXX";
        std::vector<char> temporary_buffer(
            temporary_template.begin(),
            temporary_template.end()
        );
        temporary_buffer.push_back('\0');
        const int temporary_descriptor = ::mkstemp(temporary_buffer.data());
        if (temporary_descriptor < 0) {
            throw std::runtime_error(
                "Unable to create an exclusive migration file: " +
                std::string(std::strerror(errno))
            );
        }
        ::close(temporary_descriptor);
        const std::string temporary = temporary_buffer.data();
        const auto temporary_wal = storage_v2::wal_path(temporary);
        bool temporary_owned = true;
        try {
            storage_v2::initialize_owned_empty_file(temporary, legacy.fields, legacy.rows);
            const auto migrated = storage_v2::open_table(temporary);
            if (migrated.fields != legacy.fields || migrated.rows != legacy.rows) {
                throw std::runtime_error("V2 verification does not match the v1 source");
            }
            std::filesystem::remove(temporary_wal);
            if (read_bytes(source_schema_path) != source_schema_before ||
                read_bytes(source_rows_path) != source_rows_before) {
                throw std::runtime_error("V1 source changed during migration; promotion aborted");
            }
            maybe_migration_crash("before-promotion");
            if (::link(temporary.c_str(), destination.c_str()) != 0) {
                throw std::runtime_error(
                    "Unable to promote without replacing the destination: " +
                    std::string(std::strerror(errno))
                );
            }
            maybe_migration_crash("after-promotion");
            fsync_directory(destination);
            maybe_migration_crash("after-directory-fsync");
            std::filesystem::remove(temporary);
            temporary_owned = false;
            fsync_directory(destination);
        } catch (...) {
            if (temporary_owned) {
                std::filesystem::remove(temporary);
                std::filesystem::remove(temporary_wal);
            }
            throw;
        }
        std::cout << "result: migrated and verified\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
