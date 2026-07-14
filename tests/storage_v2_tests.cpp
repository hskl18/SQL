#include "includes/storage/storage_v2.h"
#include "includes/storage/page_buffer.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Function>
void expect_corruption(Function function, const std::string& fragment) {
    try {
        function();
    } catch (const storage_v2::CorruptionError& error) {
        expect(
            std::string(error.what()).find(fragment) != std::string::npos,
            "corruption error should contain: " + fragment + ", received: " + error.what()
        );
        return;
    }
    throw std::runtime_error("operation should reject corrupt storage");
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

void write_bytes(const std::string& path, const Bytes& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

void put_u32(std::uint8_t* target, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        *target++ = static_cast<std::uint8_t>((value >> shift) & 0xffU);
    }
}

void refresh_page_checksum(Bytes& bytes, std::size_t page) {
    auto* page_bytes = bytes.data() + page * storage_v2::PAGE_SIZE;
    put_u32(page_bytes + 20, 0);
    put_u32(page_bytes + 20, crc32(page_bytes, storage_v2::PAGE_SIZE));
}

void remove_database(const std::string& path) {
    std::filesystem::remove(path);
    std::filesystem::remove(storage_v2::wal_path(path));
}

void test_page_format_and_large_rows() {
    const std::string path = "format.sql2";
    remove_database(path);
    std::vector<std::string> fields;
    std::vector<std::string> maximum_row;
    std::vector<std::string> empty_row(64);
    for (int field = 0; field < 64; ++field) {
        fields.push_back("field_" + std::to_string(field));
        maximum_row.push_back(std::string(100, static_cast<char>('a' + field % 26)));
    }
    storage_v2::create_from_rows(path, fields, {empty_row, maximum_row});
    const auto opened = storage_v2::open_table(path);
    expect(opened.fields == fields, "64-field schema should round-trip");
    expect(opened.rows.size() == 2, "large-row table should contain both rows");
    expect(opened.rows[0].size() == fields.size(), "empty-value row should retain every field");
    expect(
        std::all_of(opened.rows[0].begin(), opened.rows[0].end(), [](const std::string& value) {
            return value.empty();
        }),
        "empty values should round-trip"
    );
    expect(opened.rows[1] == maximum_row, "6,400-byte logical row should cross pages safely");
    expect(opened.inspection.format_version == 2, "format version should be 2");
    expect(opened.inspection.page_count > 4, "large row should allocate multiple pages");
    expect(read_bytes(path).size() == opened.inspection.page_count * storage_v2::PAGE_SIZE,
           "physical file size should exactly match page count");
    storage_v2::PageBuffer buffer(read_bytes(path));
    std::vector<std::uint32_t> visited;
    buffer.visit_pages([&visited](std::uint32_t page, const std::uint8_t*) {
        visited.push_back(page);
    });
    expect(visited.size() == opened.inspection.page_count,
           "buffer flush visitor should receive every owned page");
    for (std::size_t page = 0; page < visited.size(); ++page) {
        expect(visited[page] == page, "buffer flush order should be deterministic by page id");
    }
    bool bounded = false;
    try {
        static_cast<void>(buffer.page_data(static_cast<std::uint32_t>(buffer.page_count())));
    } catch (const std::out_of_range&) {
        bounded = true;
    }
    expect(bounded, "buffer reads should reject page_count as out of bounds");
    storage_v2::PageBuffer dirty_buffer(read_bytes(path), {0, 2});
    std::vector<std::uint32_t> dirty_visited;
    dirty_buffer.visit_dirty_pages(
        [&dirty_visited](std::uint32_t page, const std::uint8_t*) {
            dirty_visited.push_back(page);
        }
    );
    expect(dirty_visited == std::vector<std::uint32_t>({0, 2}),
           "dirty-page flush order should be explicit and deterministic");
    remove_database(path);
}

void test_persistent_index_property_and_reopen() {
    const std::string path = "property.sql2";
    remove_database(path);
    std::vector<std::vector<std::string>> rows;
    std::mt19937 generator(0x51A2B3C4U);
    std::uniform_int_distribution<int> value_distribution(0, 150);
    for (std::uint64_t record = 0; record < 2'000; ++record) {
        rows.push_back({
            "key_" + std::to_string(value_distribution(generator)),
            "record_" + std::to_string(record),
        });
    }
    storage_v2::create_from_rows(path, {"group", "record"}, rows);
    const auto first = storage_v2::open_table(path);
    const auto first_roots = first.inspection.index_roots;
    expect(first.rows == rows, "randomized rows should reopen exactly");
    for (std::size_t field = 0; field < 2; ++field) {
        std::vector<std::pair<std::string, std::uint64_t>> expected;
        for (std::uint64_t record = 0; record < rows.size(); ++record) {
            expected.emplace_back(rows[record][field], record);
        }
        std::sort(expected.begin(), expected.end());
        expect(first.indexes[field] == expected, "persistent index should match sorted reference");
    }
    const auto before = read_bytes(path);
    const auto second = storage_v2::open_table(path);
    expect(second.inspection.index_roots == first_roots, "reopen should retain persistent roots");
    expect(read_bytes(path) == before, "clean reopen should not rewrite or rebuild storage");
    expect(second.indexes == first.indexes, "reopen should load identical persisted index entries");
    remove_database(path);
}

void test_leaf_split_and_root_change() {
    const std::string path = "root-change.sql2";
    remove_database(path);
    std::vector<std::vector<std::string>> rows;
    for (int record = 0; record < 36; ++record) {
        auto value = "key_" + std::to_string(record);
        value.resize(100, 'x');
        rows.push_back({value});
    }
    storage_v2::create_from_rows(path, {"value"}, rows);
    const auto before = read_bytes(path);
    const auto leaf_root = storage_v2::inspect(path).index_roots[0];
    auto split_value = std::string("key_36");
    split_value.resize(100, 'x');
    storage_v2::insert_row(path, {split_value});
    const auto reopened = storage_v2::open_table(path);
    expect(reopened.inspection.index_roots[0] != leaf_root,
           "leaf split should replace the field index root");
    expect(reopened.indexes[0].size() == 37, "root split should preserve every index entry");
    const auto after = read_bytes(path);
    expect(std::equal(
               before.begin() + storage_v2::PAGE_SIZE,
               before.begin() + 2 * storage_v2::PAGE_SIZE,
               after.begin() + storage_v2::PAGE_SIZE
           ),
           "incremental insert should leave the schema page byte-identical");
    remove_database(path);
}

void test_incremental_randomized_insert_and_internal_split() {
    const std::string path = "incremental-property.sql2";
    remove_database(path);
    storage_v2::create_table(path, {"value"});
    std::vector<std::vector<std::string>> rows;
    std::mt19937 generator(0x1A2B3C4DU);
    std::uniform_int_distribution<int> distribution(0, 40);
    for (int record = 0; record < 120; ++record) {
        const auto value = "value_" + std::to_string(distribution(generator));
        storage_v2::insert_row(path, {value});
        rows.push_back({value});
        if (record % 20 == 19) {
            const auto reopened = storage_v2::open_table(path);
            expect(reopened.rows == rows, "incremental reopen should preserve randomized rows");
            std::vector<std::pair<std::string, std::uint64_t>> expected;
            for (std::uint64_t index = 0; index < rows.size(); ++index) {
                expected.emplace_back(rows[index][0], index);
            }
            std::sort(expected.begin(), expected.end());
            expect(reopened.indexes[0] == expected,
                   "incremental persistent tree should match the sorted reference");
        }
    }
    const auto matches = storage_v2::query_index(path, 0, ">=", "value_30");
    std::vector<std::uint64_t> expected_matches;
    std::vector<std::pair<std::string, std::uint64_t>> ordered;
    for (std::uint64_t index = 0; index < rows.size(); ++index) {
        ordered.emplace_back(rows[index][0], index);
    }
    std::sort(ordered.begin(), ordered.end());
    for (const auto& entry : ordered) {
        if (entry.first >= "value_30") expected_matches.push_back(entry.second);
    }
    expect(matches == expected_matches, "page-backed range query should match the reference");
    remove_database(path);

    const std::string deep_path = "internal-root-change.sql2";
    remove_database(deep_path);
    std::vector<std::vector<std::string>> deep_rows;
    for (int record = 0; record < 1'296; ++record) {
        auto value = std::to_string(record);
        value.insert(value.begin(), 100 - value.size(), '0');
        deep_rows.push_back({value});
    }
    storage_v2::create_from_rows(deep_path, {"value"}, deep_rows);
    const auto old_root = storage_v2::inspect(deep_path).index_roots[0];
    const auto old_page_count = storage_v2::inspect(deep_path).page_count;
    storage_v2::insert_row(deep_path, {std::string(100, 'z')});
    const auto deep = storage_v2::open_table(deep_path);
    expect(deep.inspection.index_roots[0] != old_root,
           "incremental internal split should replace the root");
    expect(deep.inspection.page_count >= old_page_count + 3,
           "internal split should allocate a leaf, sibling internal node, and root");
    expect(deep.indexes[0].size() == 1'297,
           "internal root split should preserve every persistent entry");
    remove_database(deep_path);
}

void test_corruption_fails_closed() {
    const std::string path = "corruption.sql2";
    remove_database(path);
    std::vector<std::vector<std::string>> rows;
    for (int record = 0; record < 2'000; ++record) {
        rows.push_back({std::string(100, static_cast<char>('a' + record % 26)) + std::to_string(record)});
        rows.back()[0].resize(100);
    }
    storage_v2::create_from_rows(path, {"value"}, rows);
    const auto valid = read_bytes(path);

    auto checksum_corrupt = valid;
    checksum_corrupt[storage_v2::PAGE_SIZE + 40] ^= 0x5aU;
    write_bytes(path, checksum_corrupt);
    expect_corruption([&]() { static_cast<void>(storage_v2::open_table(path)); }, "checksum");

    auto count_corrupt = valid;
    put_u32(count_corrupt.data() + 32 + 12, 1);
    refresh_page_checksum(count_corrupt, 0);
    write_bytes(path, count_corrupt);
    expect_corruption([&]() { static_cast<void>(storage_v2::open_table(path)); }, "page count");

    auto child_corrupt = valid;
    std::size_t internal_page = 0;
    for (std::size_t page = 0; page < child_corrupt.size() / storage_v2::PAGE_SIZE; ++page) {
        if (child_corrupt[page * storage_v2::PAGE_SIZE + 4] == 6) {
            internal_page = page;
            break;
        }
    }
    expect(internal_page != 0, "property fixture should contain an internal index page");
    put_u32(child_corrupt.data() + internal_page * storage_v2::PAGE_SIZE + 32 + 4, 0);
    refresh_page_checksum(child_corrupt, internal_page);
    write_bytes(path, child_corrupt);
    expect_corruption([&]() { static_cast<void>(storage_v2::open_table(path)); }, "index");

    write_bytes(path, valid);
    {
        std::ofstream wal(storage_v2::wal_path(path), std::ios::binary | std::ios::trunc);
        wal << "partial";
    }
    const auto database_before = read_bytes(path);
    expect_corruption([&]() { static_cast<void>(storage_v2::open_table(path)); }, "incomplete");
    expect(read_bytes(path) == database_before, "invalid WAL must not modify the database");
    remove_database(path);
}

} // namespace

int main() {
    try {
        test_page_format_and_large_rows();
        std::cout << "PASS: page format and large rows\n";
        test_persistent_index_property_and_reopen();
        std::cout << "PASS: persistent randomized indexes and reopen\n";
        test_leaf_split_and_root_change();
        std::cout << "PASS: persistent leaf split and root change\n";
        test_incremental_randomized_insert_and_internal_split();
        std::cout << "PASS: incremental randomized inserts and internal split\n";
        test_corruption_fails_closed();
        std::cout << "PASS: page, pointer, and WAL corruption fail closed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
