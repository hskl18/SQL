#include "storage_v2.h"
#include "page_buffer.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace storage_v2 {
namespace {

constexpr std::size_t PAGE_HEADER_SIZE = 32;
constexpr std::size_t PAGE_PAYLOAD_SIZE = PAGE_SIZE - PAGE_HEADER_SIZE;
constexpr std::uint32_t NO_PAGE = std::numeric_limits<std::uint32_t>::max();
constexpr std::size_t MAX_FIELDS = 64;
constexpr std::size_t MAX_VALUE_LENGTH = 100;

enum class PageType : std::uint8_t {
    Superblock = 1,
    Schema = 2,
    Rows = 3,
    IndexDirectory = 4,
    IndexLeaf = 5,
    IndexInternal = 6,
};

using Bytes = std::vector<std::uint8_t>;
using PageBytes = std::array<std::uint8_t, PAGE_SIZE>;

struct Page {
    PageType type;
    std::uint32_t id;
    std::uint32_t next = NO_PAGE;
    Bytes payload;
};

struct Superblock {
    std::uint64_t generation = 0;
    std::uint32_t page_count = 0;
    std::uint32_t schema_head = NO_PAGE;
    std::uint32_t rows_head = NO_PAGE;
    std::uint32_t index_directory_head = NO_PAGE;
    std::uint32_t free_head = NO_PAGE;
    std::uint32_t field_count = 0;
    std::uint64_t row_count = 0;
};

struct ChildDescriptor {
    std::uint32_t id;
    std::string minimum_key;
    std::uint64_t minimum_record;
    std::uint16_t level;
};

struct ParsedImage {
    LoadedTable table;
    std::vector<Page> pages;
};

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

void append_u16(Bytes& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}

void append_u32(Bytes& output, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

void append_u64(Bytes& output, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

void append_string(Bytes& output, const std::string& value) {
    if (value.size() > MAX_VALUE_LENGTH) {
        throw std::length_error("Values must not exceed 100 bytes");
    }
    append_u16(output, static_cast<std::uint16_t>(value.size()));
    output.insert(output.end(), value.begin(), value.end());
}

class Reader {
public:
    Reader(const Bytes& bytes, const std::string& context)
        : bytes_(bytes), context_(context) {}

    std::uint16_t u16() {
        require(2);
        const auto value = static_cast<std::uint16_t>(bytes_[position_]) |
            (static_cast<std::uint16_t>(bytes_[position_ + 1]) << 8U);
        position_ += 2;
        return value;
    }

    std::uint32_t u32() {
        require(4);
        std::uint32_t value = 0;
        for (int shift = 0; shift < 32; shift += 8) {
            value |= static_cast<std::uint32_t>(bytes_[position_++]) << shift;
        }
        return value;
    }

    std::uint64_t u64() {
        require(8);
        std::uint64_t value = 0;
        for (int shift = 0; shift < 64; shift += 8) {
            value |= static_cast<std::uint64_t>(bytes_[position_++]) << shift;
        }
        return value;
    }

    std::string string() {
        const auto length = u16();
        if (length > MAX_VALUE_LENGTH) {
            throw CorruptionError(context_ + " string exceeds 100 bytes");
        }
        require(length);
        std::string value(
            bytes_.begin() + static_cast<std::ptrdiff_t>(position_),
            bytes_.begin() + static_cast<std::ptrdiff_t>(position_ + length)
        );
        position_ += length;
        return value;
    }

    std::size_t remaining() const { return bytes_.size() - position_; }
    std::size_t position() const { return position_; }

private:
    void require(std::size_t count) const {
        if (count > bytes_.size() - position_) {
            throw CorruptionError(context_ + " contains a partial record");
        }
    }

    const Bytes& bytes_;
    std::string context_;
    std::size_t position_ = 0;
};

void validate_schema(
    const std::vector<std::string>& fields,
    const std::vector<std::vector<std::string>>& rows
) {
    if (fields.empty() || fields.size() > MAX_FIELDS) {
        throw std::invalid_argument("A table must have between 1 and 64 fields");
    }
    std::set<std::string> unique;
    for (const auto& field : fields) {
        if (field.empty() || field.size() > MAX_VALUE_LENGTH) {
            throw std::invalid_argument("Field names must be between 1 and 100 bytes");
        }
        if (!unique.insert(field).second) {
            throw std::invalid_argument("Duplicate field name: " + field);
        }
    }
    for (const auto& row : rows) {
        if (row.size() != fields.size()) {
            throw std::invalid_argument("Stored row field count does not match schema");
        }
        for (const auto& value : row) {
            if (value.size() > MAX_VALUE_LENGTH) {
                throw std::length_error("Values must not exceed 100 bytes");
            }
        }
    }
}

PageBytes encode_page(const Page& page) {
    if (page.payload.size() > PAGE_PAYLOAD_SIZE) {
        throw std::logic_error("Page payload exceeds its bound");
    }
    PageBytes output{};
    output[0] = 'S';
    output[1] = 'Q';
    output[2] = 'L';
    output[3] = '2';
    output[4] = static_cast<std::uint8_t>(page.type);
    output[5] = 0;
    output[6] = static_cast<std::uint8_t>(FORMAT_VERSION & 0xffU);
    output[7] = static_cast<std::uint8_t>(FORMAT_VERSION >> 8U);
    const auto put32 = [&output](std::size_t offset, std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            output[offset++] = static_cast<std::uint8_t>((value >> shift) & 0xffU);
        }
    };
    put32(8, page.id);
    put32(12, static_cast<std::uint32_t>(page.payload.size()));
    put32(16, page.next);
    std::copy(page.payload.begin(), page.payload.end(), output.begin() + PAGE_HEADER_SIZE);
    put32(20, crc32(output.data(), output.size()));
    return output;
}

std::uint32_t read_u32_at(const std::uint8_t* bytes) {
    std::uint32_t value = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(*bytes++) << shift;
    }
    return value;
}

Page decode_page(const std::uint8_t* bytes, std::uint32_t expected_id) {
    if (!(bytes[0] == 'S' && bytes[1] == 'Q' && bytes[2] == 'L' && bytes[3] == '2')) {
        throw CorruptionError("page " + std::to_string(expected_id) + " has invalid magic");
    }
    const auto version = static_cast<std::uint16_t>(bytes[6]) |
        (static_cast<std::uint16_t>(bytes[7]) << 8U);
    if (version != FORMAT_VERSION) {
        throw CorruptionError("page " + std::to_string(expected_id) + " has unsupported version");
    }
    if (bytes[5] != 0) {
        throw CorruptionError("page " + std::to_string(expected_id) + " has unsupported flags");
    }
    for (std::size_t i = 24; i < PAGE_HEADER_SIZE; ++i) {
        if (bytes[i] != 0) {
            throw CorruptionError("page " + std::to_string(expected_id) + " reserved bytes are not zero");
        }
    }
    if (read_u32_at(bytes + 8) != expected_id) {
        throw CorruptionError("page identifier does not match its file offset");
    }
    const auto payload_size = read_u32_at(bytes + 12);
    if (payload_size > PAGE_PAYLOAD_SIZE) {
        throw CorruptionError("page payload exceeds its bound");
    }
    for (std::size_t i = PAGE_HEADER_SIZE + payload_size; i < PAGE_SIZE; ++i) {
        if (bytes[i] != 0) {
            throw CorruptionError("page " + std::to_string(expected_id) + " unused bytes are not zero");
        }
    }
    PageBytes checksum_page{};
    std::copy(bytes, bytes + PAGE_SIZE, checksum_page.begin());
    const auto expected_checksum = read_u32_at(bytes + 20);
    std::fill(checksum_page.begin() + 20, checksum_page.begin() + 24, 0);
    if (crc32(checksum_page.data(), checksum_page.size()) != expected_checksum) {
        throw CorruptionError("page " + std::to_string(expected_id) + " checksum mismatch");
    }
    const auto raw_type = bytes[4];
    if (raw_type < static_cast<std::uint8_t>(PageType::Superblock) ||
        raw_type > static_cast<std::uint8_t>(PageType::IndexInternal)) {
        throw CorruptionError("page " + std::to_string(expected_id) + " has invalid type");
    }
    Page page{
        static_cast<PageType>(raw_type),
        expected_id,
        read_u32_at(bytes + 16),
        {},
    };
    page.payload.assign(bytes + PAGE_HEADER_SIZE, bytes + PAGE_HEADER_SIZE + payload_size);
    return page;
}

std::uint32_t add_chain(
    std::vector<Page>& pages,
    PageType type,
    const Bytes& bytes
) {
    if (bytes.empty()) {
        Page page{type, static_cast<std::uint32_t>(pages.size()), NO_PAGE, {}};
        pages.push_back(page);
        return page.id;
    }
    const auto first = static_cast<std::uint32_t>(pages.size());
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto size = std::min(PAGE_PAYLOAD_SIZE, bytes.size() - offset);
        Page page{
            type,
            static_cast<std::uint32_t>(pages.size()),
            NO_PAGE,
            Bytes(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                  bytes.begin() + static_cast<std::ptrdiff_t>(offset + size)),
        };
        if (!pages.empty() && pages.back().type == type && pages.back().next == NO_PAGE &&
            pages.back().id >= first) {
            pages.back().next = page.id;
        }
        pages.push_back(std::move(page));
        offset += size;
    }
    return first;
}

std::uint32_t build_index(
    std::vector<Page>& pages,
    const std::vector<std::pair<std::string, std::uint64_t>>& entries
) {
    if (entries.empty()) return NO_PAGE;

    std::vector<ChildDescriptor> level;
    std::vector<std::size_t> leaf_positions;
    std::size_t cursor = 0;
    while (cursor < entries.size()) {
        Bytes payload;
        append_u16(payload, 0);
        append_u32(payload, NO_PAGE);
        append_u16(payload, 0);
        std::uint16_t count = 0;
        const auto first_entry = entries[cursor];
        while (cursor < entries.size()) {
            const auto entry_size = 2U + entries[cursor].first.size() + 8U;
            if (payload.size() + entry_size > PAGE_PAYLOAD_SIZE) break;
            append_string(payload, entries[cursor].first);
            append_u64(payload, entries[cursor].second);
            ++count;
            ++cursor;
        }
        if (count == 0) throw std::logic_error("Index entry does not fit in a page");
        payload[0] = static_cast<std::uint8_t>(count & 0xffU);
        payload[1] = static_cast<std::uint8_t>(count >> 8U);
        const auto id = static_cast<std::uint32_t>(pages.size());
        leaf_positions.push_back(pages.size());
        pages.push_back(Page{PageType::IndexLeaf, id, NO_PAGE, std::move(payload)});
        level.push_back(ChildDescriptor{id, first_entry.first, first_entry.second, 0});
    }
    for (std::size_t i = 0; i < leaf_positions.size(); ++i) {
        auto& leaf = pages[leaf_positions[i]];
        leaf.next = i + 1 < leaf_positions.size()
            ? pages[leaf_positions[i + 1]].id
            : NO_PAGE;
        const auto previous = i == 0 ? NO_PAGE : pages[leaf_positions[i - 1]].id;
        for (int shift = 0; shift < 32; shift += 8) {
            leaf.payload[2 + static_cast<std::size_t>(shift / 8)] =
                static_cast<std::uint8_t>((previous >> shift) & 0xffU);
        }
    }

    while (level.size() > 1) {
        std::vector<ChildDescriptor> parents;
        std::size_t child = 0;
        while (child < level.size()) {
            const auto begin = child;
            std::size_t end = child + 1;
            std::size_t encoded_size = 8;
            while (end < level.size()) {
                const auto entry_size = 2U + level[end].minimum_key.size() + 8U + 4U;
                if (encoded_size + entry_size > PAGE_PAYLOAD_SIZE) break;
                encoded_size += entry_size;
                ++end;
            }
            if (level.size() - end == 1 && end - begin > 2) --end;
            if (end - begin < 2) {
                throw std::logic_error("Internal index page must contain at least two children");
            }
            Bytes payload;
            append_u16(payload, static_cast<std::uint16_t>(level[child].level + 1));
            append_u16(payload, 0);
            append_u32(payload, level[child].id);
            ++child;
            std::uint16_t key_count = 0;
            while (child < end) {
                append_string(payload, level[child].minimum_key);
                append_u64(payload, level[child].minimum_record);
                append_u32(payload, level[child].id);
                ++key_count;
                ++child;
            }
            payload[2] = static_cast<std::uint8_t>(key_count & 0xffU);
            payload[3] = static_cast<std::uint8_t>(key_count >> 8U);
            const auto id = static_cast<std::uint32_t>(pages.size());
            pages.push_back(Page{PageType::IndexInternal, id, NO_PAGE, std::move(payload)});
            parents.push_back(ChildDescriptor{
                id,
                level[begin].minimum_key,
                level[begin].minimum_record,
                static_cast<std::uint16_t>(level[begin].level + 1),
            });
        }
        level = std::move(parents);
    }
    return level.front().id;
}

Bytes build_image(
    const std::vector<std::string>& fields,
    const std::vector<std::vector<std::string>>& rows,
    std::uint64_t generation
) {
    validate_schema(fields, rows);
    std::vector<Page> pages;
    pages.push_back(Page{PageType::Superblock, 0, NO_PAGE, {}});

    Bytes schema;
    append_u32(schema, static_cast<std::uint32_t>(fields.size()));
    for (const auto& field : fields) append_string(schema, field);
    const auto schema_head = add_chain(pages, PageType::Schema, schema);

    Bytes row_bytes;
    append_u64(row_bytes, static_cast<std::uint64_t>(rows.size()));
    for (const auto& row : rows) {
        append_u32(row_bytes, static_cast<std::uint32_t>(row.size()));
        for (const auto& value : row) append_string(row_bytes, value);
    }
    const auto rows_head = add_chain(pages, PageType::Rows, row_bytes);

    const auto index_directory_position = pages.size();
    const auto index_directory_head = static_cast<std::uint32_t>(pages.size());
    pages.push_back(Page{PageType::IndexDirectory, index_directory_head, NO_PAGE, {}});

    std::vector<std::uint32_t> roots;
    roots.reserve(fields.size());
    for (std::size_t field = 0; field < fields.size(); ++field) {
        std::vector<std::pair<std::string, std::uint64_t>> entries;
        entries.reserve(rows.size());
        for (std::size_t row = 0; row < rows.size(); ++row) {
            entries.emplace_back(rows[row][field], static_cast<std::uint64_t>(row));
        }
        std::sort(entries.begin(), entries.end());
        roots.push_back(build_index(pages, entries));
    }

    Bytes index_directory;
    append_u32(index_directory, static_cast<std::uint32_t>(roots.size()));
    for (const auto root : roots) append_u32(index_directory, root);
    pages[index_directory_position].payload = std::move(index_directory);

    Bytes superblock;
    append_u64(superblock, generation);
    append_u32(superblock, PAGE_SIZE);
    append_u32(superblock, static_cast<std::uint32_t>(pages.size()));
    append_u32(superblock, schema_head);
    append_u32(superblock, rows_head);
    append_u32(superblock, index_directory_head);
    append_u32(superblock, NO_PAGE);
    append_u32(superblock, static_cast<std::uint32_t>(fields.size()));
    append_u64(superblock, static_cast<std::uint64_t>(rows.size()));
    pages[0].payload = std::move(superblock);

    Bytes image;
    image.reserve(pages.size() * PAGE_SIZE);
    for (const auto& page : pages) {
        const auto encoded = encode_page(page);
        image.insert(image.end(), encoded.begin(), encoded.end());
    }
    return image;
}

Bytes read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Table does not exist: " + path);
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    if (length < 0) throw std::runtime_error("Unable to determine file size: " + path);
    input.seekg(0);
    Bytes bytes(static_cast<std::size_t>(length));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
            throw std::runtime_error("Unable to read complete file: " + path);
        }
    }
    return bytes;
}

Bytes read_chain(
    const std::vector<Page>& pages,
    std::uint32_t head,
    PageType expected_type,
    std::set<std::uint32_t>& owned
) {
    Bytes output;
    std::set<std::uint32_t> chain;
    auto current = head;
    while (current != NO_PAGE) {
        if (current >= pages.size()) throw CorruptionError("page chain points outside the file");
        if (!chain.insert(current).second) throw CorruptionError("page chain contains a cycle");
        if (!owned.insert(current).second) throw CorruptionError("page belongs to multiple structures");
        const auto& page = pages[current];
        if (page.type != expected_type) throw CorruptionError("page chain contains the wrong page type");
        output.insert(output.end(), page.payload.begin(), page.payload.end());
        current = page.next;
    }
    return output;
}

struct ValidatedNode {
    std::string minimum_key;
    std::string maximum_key;
    std::uint16_t level = 0;
    std::vector<std::pair<std::string, std::uint64_t>> entries;
    std::vector<std::uint32_t> leaves;
};

ValidatedNode validate_index_node(
    const std::vector<Page>& pages,
    std::uint32_t id,
    std::set<std::uint32_t>& owned,
    std::set<std::uint32_t>& active
) {
    if (id >= pages.size()) throw CorruptionError("index child pointer is outside the file");
    if (!active.insert(id).second) throw CorruptionError("index contains a child cycle");
    if (!owned.insert(id).second) throw CorruptionError("index page is referenced more than once");
    const auto& page = pages[id];
    ValidatedNode result;
    if (page.type == PageType::IndexLeaf) {
        Reader reader(page.payload, "index leaf");
        const auto count = reader.u16();
        static_cast<void>(reader.u32());
        if (reader.u16() != 0) throw CorruptionError("index leaf reserved value is not zero");
        if (count == 0) throw CorruptionError("index leaf is empty");
        for (std::uint16_t i = 0; i < count; ++i) {
            result.entries.emplace_back(reader.string(), reader.u64());
        }
        if (reader.remaining() != 0) throw CorruptionError("index leaf has trailing bytes");
        if (!std::is_sorted(result.entries.begin(), result.entries.end())) {
            throw CorruptionError("index leaf entries are not ordered");
        }
        result.minimum_key = result.entries.front().first;
        result.maximum_key = result.entries.back().first;
        result.leaves.push_back(id);
    } else if (page.type == PageType::IndexInternal) {
        if (page.next != NO_PAGE) throw CorruptionError("internal index page has a sibling link");
        Reader reader(page.payload, "internal index page");
        const auto level = reader.u16();
        const auto key_count = reader.u16();
        if (level == 0 || key_count == 0) throw CorruptionError("internal index key count is invalid");
        std::vector<std::pair<std::string, std::uint64_t>> separators;
        std::vector<std::uint32_t> children;
        children.push_back(reader.u32());
        for (std::uint16_t i = 0; i < key_count; ++i) {
            const auto key = reader.string();
            separators.emplace_back(key, reader.u64());
            children.push_back(reader.u32());
        }
        if (reader.remaining() != 0) throw CorruptionError("internal index page has trailing bytes");
        if (!std::is_sorted(separators.begin(), separators.end())) {
            throw CorruptionError("internal index separators are not ordered");
        }
        result.level = level;
        for (std::size_t child_index = 0; child_index < children.size(); ++child_index) {
            auto child = validate_index_node(pages, children[child_index], owned, active);
            if (child.level + 1 != level) throw CorruptionError("index child level is invalid");
            if (child_index > 0 && separators[child_index - 1] != child.entries.front()) {
                throw CorruptionError("index separator does not match child minimum");
            }
            if (!result.entries.empty() && result.entries.back() > child.entries.front()) {
                throw CorruptionError("index child ranges overlap out of order");
            }
            result.entries.insert(result.entries.end(), child.entries.begin(), child.entries.end());
            result.leaves.insert(result.leaves.end(), child.leaves.begin(), child.leaves.end());
            if (child_index == 0) result.minimum_key = child.minimum_key;
            result.maximum_key = child.maximum_key;
        }
    } else {
        throw CorruptionError("index root or child has the wrong page type");
    }
    active.erase(id);
    return result;
}

ParsedImage parse_image(const Bytes& image, bool wal_present) {
    if (image.empty() || image.size() % PAGE_SIZE != 0) {
        throw CorruptionError("database file is not a complete page image");
    }
    const auto page_count = image.size() / PAGE_SIZE;
    if (page_count > std::numeric_limits<std::uint32_t>::max()) {
        throw CorruptionError("database contains too many pages");
    }
    std::vector<Page> pages;
    pages.reserve(page_count);
    for (std::uint32_t id = 0; id < page_count; ++id) {
        pages.push_back(decode_page(image.data() + static_cast<std::size_t>(id) * PAGE_SIZE, id));
    }
    if (pages[0].type != PageType::Superblock || pages[0].next != NO_PAGE) {
        throw CorruptionError("page zero is not a standalone superblock");
    }
    Reader super_reader(pages[0].payload, "superblock");
    Superblock super;
    super.generation = super_reader.u64();
    if (super_reader.u32() != PAGE_SIZE) throw CorruptionError("superblock page size is invalid");
    super.page_count = super_reader.u32();
    super.schema_head = super_reader.u32();
    super.rows_head = super_reader.u32();
    super.index_directory_head = super_reader.u32();
    super.free_head = super_reader.u32();
    super.field_count = super_reader.u32();
    super.row_count = super_reader.u64();
    if (super_reader.remaining() != 0) throw CorruptionError("superblock has trailing bytes");
    if (super.page_count != pages.size()) throw CorruptionError("superblock page count is invalid");
    if (super.free_head != NO_PAGE) throw CorruptionError("unsupported non-empty free-page list");
    if (super.field_count == 0 || super.field_count > MAX_FIELDS) {
        throw CorruptionError("superblock field count is invalid");
    }

    std::set<std::uint32_t> owned{0};
    const auto schema_bytes = read_chain(pages, super.schema_head, PageType::Schema, owned);
    Reader schema_reader(schema_bytes, "schema");
    if (schema_reader.u32() != super.field_count) throw CorruptionError("schema field count mismatch");
    std::vector<std::string> fields;
    for (std::uint32_t i = 0; i < super.field_count; ++i) fields.push_back(schema_reader.string());
    if (schema_reader.remaining() != 0) throw CorruptionError("schema has trailing bytes");
    std::set<std::string> unique_fields(fields.begin(), fields.end());
    if (unique_fields.size() != fields.size() || unique_fields.count("") != 0) {
        throw CorruptionError("schema contains empty or duplicate fields");
    }

    const auto row_bytes = read_chain(pages, super.rows_head, PageType::Rows, owned);
    Reader row_reader(row_bytes, "row stream");
    if (row_reader.u64() != super.row_count) throw CorruptionError("row count mismatch");
    std::vector<std::vector<std::string>> rows;
    if (super.row_count > std::numeric_limits<std::size_t>::max()) {
        throw CorruptionError("row count exceeds addressable memory");
    }
    rows.reserve(static_cast<std::size_t>(super.row_count));
    for (std::uint64_t row = 0; row < super.row_count; ++row) {
        if (row_reader.u32() != super.field_count) throw CorruptionError("stored row field count mismatch");
        std::vector<std::string> values;
        values.reserve(super.field_count);
        for (std::uint32_t field = 0; field < super.field_count; ++field) {
            values.push_back(row_reader.string());
        }
        rows.push_back(std::move(values));
    }
    if (row_reader.remaining() != 0) throw CorruptionError("row stream has trailing bytes");

    const auto directory_bytes = read_chain(
        pages,
        super.index_directory_head,
        PageType::IndexDirectory,
        owned
    );
    Reader directory_reader(directory_bytes, "index directory");
    if (directory_reader.u32() != super.field_count) {
        throw CorruptionError("index directory field count mismatch");
    }
    std::vector<std::uint32_t> roots;
    for (std::uint32_t i = 0; i < super.field_count; ++i) roots.push_back(directory_reader.u32());
    if (directory_reader.remaining() != 0) throw CorruptionError("index directory has trailing bytes");

    std::vector<std::vector<std::pair<std::string, std::uint64_t>>> indexes;
    indexes.reserve(fields.size());
    for (std::size_t field = 0; field < fields.size(); ++field) {
        if (roots[field] == NO_PAGE) {
            if (!rows.empty()) throw CorruptionError("non-empty table has an empty index root");
            indexes.emplace_back();
            continue;
        }
        std::set<std::uint32_t> active;
        auto node = validate_index_node(pages, roots[field], owned, active);
        if (node.entries.size() != rows.size()) throw CorruptionError("index entry count mismatch");
        for (std::size_t leaf = 0; leaf < node.leaves.size(); ++leaf) {
            const auto& page = pages[node.leaves[leaf]];
            Reader leaf_reader(page.payload, "index leaf");
            static_cast<void>(leaf_reader.u16());
            const auto previous = leaf_reader.u32();
            const auto expected_previous = leaf == 0 ? NO_PAGE : node.leaves[leaf - 1];
            const auto expected_next = leaf + 1 < node.leaves.size() ? node.leaves[leaf + 1] : NO_PAGE;
            if (previous != expected_previous || page.next != expected_next) {
                throw CorruptionError("index leaf sibling chain is invalid");
            }
        }
        std::vector<bool> seen(rows.size(), false);
        for (const auto& entry : node.entries) {
            if (entry.second >= rows.size()) throw CorruptionError("index record identifier is out of range");
            const auto record = static_cast<std::size_t>(entry.second);
            if (seen[record]) throw CorruptionError("index record identifier is duplicated");
            seen[record] = true;
            if (entry.first != rows[record][field]) throw CorruptionError("index key does not match stored row");
        }
        indexes.push_back(std::move(node.entries));
    }
    if (owned.size() != pages.size()) throw CorruptionError("database contains unreferenced pages");

    Inspection inspection;
    inspection.generation = super.generation;
    inspection.page_count = super.page_count;
    inspection.row_count = super.row_count;
    inspection.field_count = super.field_count;
    inspection.index_roots = roots;
    inspection.wal_present = wal_present;
    return ParsedImage{LoadedTable{fields, rows, indexes, inspection}, std::move(pages)};
}

void write_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    if (offset + 4 > bytes.size()) throw std::logic_error("u32 write exceeds payload");
    for (int shift = 0; shift < 32; shift += 8) {
        bytes[offset++] = static_cast<std::uint8_t>((value >> shift) & 0xffU);
    }
}

void write_u64(Bytes& bytes, std::size_t offset, std::uint64_t value) {
    if (offset + 8 > bytes.size()) throw std::logic_error("u64 write exceeds payload");
    for (int shift = 0; shift < 64; shift += 8) {
        bytes[offset++] = static_cast<std::uint8_t>((value >> shift) & 0xffU);
    }
}

Bytes encode_pages(const std::vector<Page>& pages) {
    Bytes image;
    image.reserve(pages.size() * PAGE_SIZE);
    for (const auto& page : pages) {
        const auto encoded = encode_page(page);
        image.insert(image.end(), encoded.begin(), encoded.end());
    }
    return image;
}

struct LeafContents {
    std::uint32_t previous;
    std::vector<std::pair<std::string, std::uint64_t>> entries;
};

LeafContents decode_leaf_contents(const Page& page) {
    if (page.type != PageType::IndexLeaf) throw std::logic_error("Expected an index leaf");
    Reader reader(page.payload, "index leaf");
    const auto count = reader.u16();
    const auto previous = reader.u32();
    if (reader.u16() != 0) throw CorruptionError("index leaf reserved value is not zero");
    std::vector<std::pair<std::string, std::uint64_t>> entries;
    entries.reserve(count);
    for (std::uint16_t index = 0; index < count; ++index) {
        entries.emplace_back(reader.string(), reader.u64());
    }
    if (reader.remaining() != 0) throw CorruptionError("index leaf has trailing bytes");
    return LeafContents{previous, std::move(entries)};
}

Bytes encode_leaf_payload(
    std::uint32_t previous,
    const std::vector<std::pair<std::string, std::uint64_t>>& entries
) {
    if (entries.empty() || entries.size() > std::numeric_limits<std::uint16_t>::max()) {
        throw std::logic_error("Index leaf entry count is invalid");
    }
    Bytes payload;
    append_u16(payload, static_cast<std::uint16_t>(entries.size()));
    append_u32(payload, previous);
    append_u16(payload, 0);
    for (const auto& entry : entries) {
        append_string(payload, entry.first);
        append_u64(payload, entry.second);
    }
    return payload;
}

struct InternalContents {
    std::uint16_t level;
    std::vector<std::pair<std::string, std::uint64_t>> separators;
    std::vector<std::uint32_t> children;
};

InternalContents decode_internal_contents(const Page& page) {
    if (page.type != PageType::IndexInternal) throw std::logic_error("Expected an internal index page");
    Reader reader(page.payload, "internal index page");
    const auto level = reader.u16();
    const auto count = reader.u16();
    std::vector<std::pair<std::string, std::uint64_t>> separators;
    std::vector<std::uint32_t> children;
    separators.reserve(count);
    children.reserve(static_cast<std::size_t>(count) + 1);
    children.push_back(reader.u32());
    for (std::uint16_t index = 0; index < count; ++index) {
        const auto key = reader.string();
        separators.emplace_back(key, reader.u64());
        children.push_back(reader.u32());
    }
    if (reader.remaining() != 0) throw CorruptionError("internal index page has trailing bytes");
    return InternalContents{level, std::move(separators), std::move(children)};
}

Bytes encode_internal_payload(const InternalContents& contents) {
    if (contents.level == 0 || contents.children.size() < 2 ||
        contents.separators.size() + 1 != contents.children.size() ||
        contents.separators.size() > std::numeric_limits<std::uint16_t>::max()) {
        throw std::logic_error("Internal index shape is invalid");
    }
    Bytes payload;
    append_u16(payload, contents.level);
    append_u16(payload, static_cast<std::uint16_t>(contents.separators.size()));
    append_u32(payload, contents.children.front());
    for (std::size_t index = 0; index < contents.separators.size(); ++index) {
        append_string(payload, contents.separators[index].first);
        append_u64(payload, contents.separators[index].second);
        append_u32(payload, contents.children[index + 1]);
    }
    return payload;
}

struct NodeSplit {
    std::pair<std::string, std::uint64_t> separator;
    std::uint32_t right_page;
};

std::uint16_t node_level(const Page& page) {
    if (page.type == PageType::IndexLeaf) return 0;
    return decode_internal_contents(page).level;
}

std::optional<NodeSplit> insert_index_entry(
    std::vector<Page>& pages,
    std::uint32_t page_id,
    const std::pair<std::string, std::uint64_t>& entry,
    std::set<std::uint32_t>& dirty
) {
    if (page_id >= pages.size()) throw CorruptionError("index insert points outside the file");
    auto& page = pages[page_id];
    if (page.type == PageType::IndexLeaf) {
        auto leaf = decode_leaf_contents(page);
        const auto position = std::lower_bound(leaf.entries.begin(), leaf.entries.end(), entry);
        if (position != leaf.entries.end() && *position == entry) {
            throw CorruptionError("index already contains the inserted record identifier");
        }
        leaf.entries.insert(position, entry);
        auto payload = encode_leaf_payload(leaf.previous, leaf.entries);
        if (payload.size() <= PAGE_PAYLOAD_SIZE) {
            page.payload = std::move(payload);
            dirty.insert(page_id);
            return std::nullopt;
        }

        const auto split_at = leaf.entries.size() / 2;
        std::vector<std::pair<std::string, std::uint64_t>> left(
            leaf.entries.begin(),
            leaf.entries.begin() + static_cast<std::ptrdiff_t>(split_at)
        );
        std::vector<std::pair<std::string, std::uint64_t>> right(
            leaf.entries.begin() + static_cast<std::ptrdiff_t>(split_at),
            leaf.entries.end()
        );
        const auto old_next = page.next;
        const auto right_id = static_cast<std::uint32_t>(pages.size());
        page.payload = encode_leaf_payload(leaf.previous, left);
        page.next = right_id;
        pages.push_back(Page{
            PageType::IndexLeaf,
            right_id,
            old_next,
            encode_leaf_payload(page_id, right),
        });
        dirty.insert(page_id);
        dirty.insert(right_id);
        if (old_next != NO_PAGE) {
            if (old_next >= pages.size() || pages[old_next].type != PageType::IndexLeaf) {
                throw CorruptionError("leaf sibling points outside its index");
            }
            write_u32(pages[old_next].payload, 2, right_id);
            dirty.insert(old_next);
        }
        return NodeSplit{right.front(), right_id};
    }
    if (page.type != PageType::IndexInternal) {
        throw CorruptionError("index insert encountered a non-index page");
    }

    auto internal = decode_internal_contents(page);
    const auto separator_position = std::upper_bound(
        internal.separators.begin(),
        internal.separators.end(),
        entry
    );
    const auto child_index = static_cast<std::size_t>(
        std::distance(internal.separators.begin(), separator_position)
    );
    const auto child_split = insert_index_entry(
        pages,
        internal.children[child_index],
        entry,
        dirty
    );
    if (!child_split.has_value()) return std::nullopt;

    internal.separators.insert(
        internal.separators.begin() + static_cast<std::ptrdiff_t>(child_index),
        child_split->separator
    );
    internal.children.insert(
        internal.children.begin() + static_cast<std::ptrdiff_t>(child_index + 1),
        child_split->right_page
    );
    auto payload = encode_internal_payload(internal);
    if (payload.size() <= PAGE_PAYLOAD_SIZE) {
        pages[page_id].payload = std::move(payload);
        dirty.insert(page_id);
        return std::nullopt;
    }

    const auto right_child_begin = internal.children.size() / 2;
    if (right_child_begin < 2 || internal.children.size() - right_child_begin < 2) {
        throw std::logic_error("Internal split cannot produce two valid nodes");
    }
    const auto promoted = internal.separators[right_child_begin - 1];
    InternalContents left{
        internal.level,
        std::vector<std::pair<std::string, std::uint64_t>>(
            internal.separators.begin(),
            internal.separators.begin() + static_cast<std::ptrdiff_t>(right_child_begin - 1)
        ),
        std::vector<std::uint32_t>(
            internal.children.begin(),
            internal.children.begin() + static_cast<std::ptrdiff_t>(right_child_begin)
        ),
    };
    InternalContents right{
        internal.level,
        std::vector<std::pair<std::string, std::uint64_t>>(
            internal.separators.begin() + static_cast<std::ptrdiff_t>(right_child_begin),
            internal.separators.end()
        ),
        std::vector<std::uint32_t>(
            internal.children.begin() + static_cast<std::ptrdiff_t>(right_child_begin),
            internal.children.end()
        ),
    };
    pages[page_id].payload = encode_internal_payload(left);
    const auto right_id = static_cast<std::uint32_t>(pages.size());
    pages.push_back(Page{
        PageType::IndexInternal,
        right_id,
        NO_PAGE,
        encode_internal_payload(right),
    });
    dirty.insert(page_id);
    dirty.insert(right_id);
    return NodeSplit{promoted, right_id};
}

struct IncrementalImage {
    Bytes image;
    std::set<std::uint32_t> dirty_pages;
    std::uint64_t generation;
};

IncrementalImage build_incremental_insert(
    const Bytes& current_image,
    const std::vector<std::string>& row
) {
    auto parsed = parse_image(current_image, false);
    if (row.size() != parsed.table.fields.size()) {
        throw std::invalid_argument(
            "Expected " + std::to_string(parsed.table.fields.size()) +
            " values, received " + std::to_string(row.size())
        );
    }
    for (const auto& value : row) {
        if (value.size() > MAX_VALUE_LENGTH) throw std::length_error("Values must not exceed 100 bytes");
    }
    if (parsed.table.inspection.generation == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("Database generation is exhausted");
    }
    if (parsed.table.rows.size() == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("Record identifier space is exhausted");
    }

    auto& pages = parsed.pages;
    std::set<std::uint32_t> dirty;
    Reader super_reader(pages[0].payload, "superblock");
    static_cast<void>(super_reader.u64());
    static_cast<void>(super_reader.u32());
    static_cast<void>(super_reader.u32());
    static_cast<void>(super_reader.u32());
    const auto rows_head = super_reader.u32();
    const auto directory_head = super_reader.u32();
    static_cast<void>(super_reader.u32());
    const auto field_count = super_reader.u32();
    const auto old_row_count = super_reader.u64();

    if (rows_head >= pages.size() || pages[rows_head].type != PageType::Rows) {
        throw CorruptionError("row chain head is invalid");
    }
    write_u64(pages[rows_head].payload, 0, old_row_count + 1);
    dirty.insert(rows_head);
    Bytes encoded_row;
    append_u32(encoded_row, field_count);
    for (const auto& value : row) append_string(encoded_row, value);
    auto row_page = rows_head;
    while (pages[row_page].next != NO_PAGE) row_page = pages[row_page].next;
    std::size_t row_offset = 0;
    while (row_offset < encoded_row.size()) {
        auto& target = pages[row_page];
        const auto capacity = PAGE_PAYLOAD_SIZE - target.payload.size();
        const auto copied = std::min(capacity, encoded_row.size() - row_offset);
        target.payload.insert(
            target.payload.end(),
            encoded_row.begin() + static_cast<std::ptrdiff_t>(row_offset),
            encoded_row.begin() + static_cast<std::ptrdiff_t>(row_offset + copied)
        );
        dirty.insert(row_page);
        row_offset += copied;
        if (row_offset < encoded_row.size()) {
            const auto next = static_cast<std::uint32_t>(pages.size());
            target.next = next;
            pages.push_back(Page{PageType::Rows, next, NO_PAGE, {}});
            dirty.insert(next);
            row_page = next;
        }
    }

    if (directory_head >= pages.size() || pages[directory_head].type != PageType::IndexDirectory) {
        throw CorruptionError("index directory head is invalid");
    }
    Reader directory_reader(pages[directory_head].payload, "index directory");
    if (directory_reader.u32() != field_count) throw CorruptionError("index field count mismatch");
    std::vector<std::uint32_t> roots;
    for (std::uint32_t field = 0; field < field_count; ++field) roots.push_back(directory_reader.u32());
    const auto record_id = old_row_count;
    for (std::size_t field = 0; field < roots.size(); ++field) {
        const auto entry = std::make_pair(row[field], record_id);
        if (roots[field] == NO_PAGE) {
            const auto root = static_cast<std::uint32_t>(pages.size());
            pages.push_back(Page{
                PageType::IndexLeaf,
                root,
                NO_PAGE,
                encode_leaf_payload(NO_PAGE, {entry}),
            });
            roots[field] = root;
            dirty.insert(root);
        } else {
            const auto old_root = roots[field];
            const auto split = insert_index_entry(pages, old_root, entry, dirty);
            if (split.has_value()) {
                const auto root = static_cast<std::uint32_t>(pages.size());
                InternalContents new_root{
                    static_cast<std::uint16_t>(node_level(pages[old_root]) + 1),
                    {split->separator},
                    {old_root, split->right_page},
                };
                pages.push_back(Page{
                    PageType::IndexInternal,
                    root,
                    NO_PAGE,
                    encode_internal_payload(new_root),
                });
                roots[field] = root;
                dirty.insert(root);
            }
        }
        write_u32(pages[directory_head].payload, 4 + field * 4, roots[field]);
    }
    dirty.insert(directory_head);

    const auto generation = parsed.table.inspection.generation + 1;
    write_u64(pages[0].payload, 0, generation);
    write_u32(pages[0].payload, 12, static_cast<std::uint32_t>(pages.size()));
    write_u64(pages[0].payload, 36, old_row_count + 1);
    dirty.insert(0);
    auto image = encode_pages(pages);
    static_cast<void>(parse_image(image, false));
    return IncrementalImage{std::move(image), std::move(dirty), generation};
}

void throw_system_error(const std::string& action, const std::string& path) {
    throw std::runtime_error(action + " " + path + ": " + std::strerror(errno));
}

void write_all(int descriptor, const std::uint8_t* data, std::size_t size) {
    while (size > 0) {
        const auto chunk = std::min(
            size,
            static_cast<std::size_t>(std::numeric_limits<ssize_t>::max())
        );
        const auto written = ::write(descriptor, data, chunk);
        if (written < 0) {
            if (errno == EINTR) continue;
            throw_system_error("Unable to write", "file descriptor");
        }
        if (written == 0) throw std::runtime_error("File write made no progress");
        data += written;
        size -= static_cast<std::size_t>(written);
    }
}

void maybe_crash(const std::string& point) {
    const char* trace_path = std::getenv("SQL_CRASH_TRACE_FILE");
    if (trace_path != nullptr) {
        const int trace = ::open(trace_path, O_WRONLY | O_CREAT | O_APPEND, 0600);
        if (trace >= 0) {
            const auto line = point + "\n";
            write_all(trace, reinterpret_cast<const std::uint8_t*>(line.data()), line.size());
            ::close(trace);
        }
    }
    const char* configured = std::getenv("SQL_CRASH_AT");
    if (configured != nullptr && point == configured) {
        ::_exit(86);
    }
}

void fsync_parent(const std::string& path) {
    auto parent = std::filesystem::path(path).parent_path();
    if (parent.empty()) parent = ".";
    const int descriptor = ::open(parent.c_str(), O_RDONLY);
    if (descriptor < 0) throw_system_error("Unable to open directory", parent.string());
    if (::fsync(descriptor) != 0) {
        const int saved = errno;
        ::close(descriptor);
        errno = saved;
        throw_system_error("Unable to fsync directory", parent.string());
    }
    ::close(descriptor);
}

Bytes wal_record(const Bytes& image, std::uint64_t generation) {
    Bytes record;
    const std::array<std::uint8_t, 8> magic{{'S', 'Q', 'L', '2', 'W', 'A', 'L', 0}};
    record.insert(record.end(), magic.begin(), magic.end());
    append_u32(record, 1);
    append_u32(record, 40);
    append_u64(record, static_cast<std::uint64_t>(image.size()));
    append_u64(record, generation);
    append_u32(record, crc32(image.data(), image.size()));
    append_u32(record, 0);
    record.insert(record.end(), image.begin(), image.end());
    const auto record_checksum = crc32(record.data(), record.size());
    const std::array<std::uint8_t, 8> commit{{'C', 'O', 'M', 'M', 'I', 'T', '2', 0}};
    record.insert(record.end(), commit.begin(), commit.end());
    append_u32(record, record_checksum);
    append_u32(record, 0);
    return record;
}

struct WalImage {
    Bytes image;
    std::uint64_t generation;
};

WalImage parse_wal(const Bytes& wal) {
    constexpr std::size_t header_size = 40;
    constexpr std::size_t footer_size = 16;
    if (wal.size() < header_size + footer_size) throw CorruptionError("WAL contains an incomplete record");
    const std::array<std::uint8_t, 8> magic{{'S', 'Q', 'L', '2', 'W', 'A', 'L', 0}};
    if (!std::equal(magic.begin(), magic.end(), wal.begin())) throw CorruptionError("WAL magic is invalid");
    Bytes header(wal.begin() + 8, wal.begin() + static_cast<std::ptrdiff_t>(header_size));
    Reader reader(header, "WAL header");
    if (reader.u32() != 1 || reader.u32() != header_size) throw CorruptionError("WAL version is invalid");
    const auto image_size = reader.u64();
    const auto generation = reader.u64();
    const auto image_checksum = reader.u32();
    if (reader.u32() != 0 || reader.remaining() != 0) throw CorruptionError("WAL header is invalid");
    if (image_size > std::numeric_limits<std::size_t>::max() ||
        image_size != wal.size() - header_size - footer_size) {
        throw CorruptionError("WAL image length is invalid");
    }
    const auto footer_offset = header_size + static_cast<std::size_t>(image_size);
    const std::array<std::uint8_t, 8> commit{{'C', 'O', 'M', 'M', 'I', 'T', '2', 0}};
    if (!std::equal(commit.begin(), commit.end(), wal.begin() + static_cast<std::ptrdiff_t>(footer_offset))) {
        throw CorruptionError("WAL commit footer is invalid");
    }
    const auto stored_record_checksum = read_u32_at(wal.data() + footer_offset + 8);
    if (read_u32_at(wal.data() + footer_offset + 12) != 0) throw CorruptionError("WAL footer is invalid");
    if (crc32(wal.data(), footer_offset) != stored_record_checksum) {
        throw CorruptionError("WAL record checksum mismatch");
    }
    Bytes image(
        wal.begin() + static_cast<std::ptrdiff_t>(header_size),
        wal.begin() + static_cast<std::ptrdiff_t>(footer_offset)
    );
    if (crc32(image.data(), image.size()) != image_checksum) throw CorruptionError("WAL image checksum mismatch");
    const auto parsed = parse_image(image, true);
    if (parsed.table.inspection.generation != generation) {
        throw CorruptionError("WAL generation does not match its database image");
    }
    return WalImage{std::move(image), generation};
}

void clear_wal(const std::string& path) {
    const int descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT, 0600);
    if (descriptor < 0) throw_system_error("Unable to clear WAL", path);
    if (::ftruncate(descriptor, 0) != 0) {
        const int saved = errno;
        ::close(descriptor);
        errno = saved;
        throw_system_error("Unable to truncate WAL", path);
    }
    maybe_crash("wal-truncate");
    if (::fsync(descriptor) != 0) {
        const int saved = errno;
        ::close(descriptor);
        errno = saved;
        throw_system_error("Unable to fsync WAL", path);
    }
    ::close(descriptor);
}

void apply_image(const std::string& path, const PageBuffer& buffer) {
    if (buffer.bytes().size() > static_cast<std::size_t>(std::numeric_limits<off_t>::max())) {
        throw std::length_error("Database image exceeds the platform file-offset range");
    }
    const bool first_creation = !std::filesystem::exists(path);
    const int descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT, 0600);
    if (descriptor < 0) throw_system_error("Unable to open database", path);
    try {
        if (::ftruncate(descriptor, static_cast<off_t>(buffer.bytes().size())) != 0) {
            throw_system_error("Unable to size database", path);
        }
        buffer.visit_dirty_pages([&](std::uint32_t page, const std::uint8_t* page_bytes) {
            const auto offset = static_cast<off_t>(static_cast<std::size_t>(page) * PAGE_SIZE);
            if (::lseek(descriptor, offset, SEEK_SET) != offset) {
                throw_system_error("Unable to seek database", path);
            }
            write_all(descriptor, page_bytes, PAGE_SIZE);
            maybe_crash("page-" + std::to_string(page));
        });
        if (::fsync(descriptor) != 0) throw_system_error("Unable to fsync database", path);
        maybe_crash("data-fsync");
    } catch (...) {
        ::close(descriptor);
        throw;
    }
    ::close(descriptor);
    if (first_creation) {
        fsync_parent(path);
        maybe_crash("database-directory-fsync");
    }
}

void commit_image(
    const std::string& path,
    const Bytes& image,
    std::uint64_t generation,
    std::set<std::uint32_t> dirty_pages = {}
) {
    const PageBuffer buffer(image, std::move(dirty_pages));
    const auto wal = wal_record(buffer.bytes(), generation);
    const auto path_wal = wal_path(path);
    const bool first_wal_creation = !std::filesystem::exists(path_wal);
    const int descriptor = ::open(path_wal.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (descriptor < 0) throw_system_error("Unable to open WAL", path_wal);
    if (const char* point = std::getenv("SQL_CRASH_AT"); point != nullptr &&
        std::string(point) == "wal-partial") {
        write_all(descriptor, wal.data(), wal.size() / 2);
        ::fsync(descriptor);
        ::_exit(86);
    }
    try {
        write_all(descriptor, wal.data(), wal.size());
        maybe_crash("wal-write");
        if (::fsync(descriptor) != 0) throw_system_error("Unable to fsync WAL", path_wal);
        maybe_crash("wal-fsync");
    } catch (...) {
        ::close(descriptor);
        throw;
    }
    ::close(descriptor);
    if (first_wal_creation) {
        fsync_parent(path_wal);
        maybe_crash("wal-directory-fsync");
    }
    apply_image(path, buffer);
    clear_wal(path_wal);
    maybe_crash("wal-clear");
}

void recover(const std::string& path) {
    const auto path_wal = wal_path(path);
    if (!std::filesystem::exists(path_wal) || std::filesystem::file_size(path_wal) == 0) return;
    const auto wal = parse_wal(read_file(path_wal));
    if (std::filesystem::exists(path)) {
        bool database_is_valid = false;
        std::uint64_t database_generation = 0;
        try {
            const auto database = parse_image(read_file(path), true);
            database_generation = database.table.inspection.generation;
            database_is_valid = true;
        } catch (const CorruptionError&) {
            // A committed WAL is allowed to replace a torn database image.
        }
        if (database_is_valid && database_generation > wal.generation) {
            throw CorruptionError("stale WAL generation would roll back the database");
        }
    }
    apply_image(path, PageBuffer(wal.image));
    clear_wal(path_wal);
}

} // namespace

std::string database_path(const std::string& table_name) {
    return table_name + ".sql2";
}

std::string wal_path(const std::string& path) {
    return path + "-wal";
}

bool exists(const std::string& path) {
    return std::filesystem::exists(path);
}

void create_table(const std::string& path, const std::vector<std::string>& fields) {
    create_from_rows(path, fields, {});
}

void create_from_rows(
    const std::string& path,
    const std::vector<std::string>& fields,
    const std::vector<std::vector<std::string>>& rows
) {
    if (std::filesystem::exists(path)) throw std::runtime_error("Table already exists: " + path);
    if (std::filesystem::exists(wal_path(path)) && std::filesystem::file_size(wal_path(path)) != 0) {
        throw std::runtime_error("Non-empty WAL already exists: " + wal_path(path));
    }
    const auto image = build_image(fields, rows, 1);
    commit_image(path, image, 1);
}

void initialize_owned_empty_file(
    const std::string& path,
    const std::vector<std::string>& fields,
    const std::vector<std::vector<std::string>>& rows
) {
    if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) != 0) {
        throw std::runtime_error("Owned migration file is not an empty regular file: " + path);
    }
    if (std::filesystem::exists(wal_path(path))) {
        throw std::runtime_error("Owned migration WAL already exists: " + wal_path(path));
    }
    const auto image = build_image(fields, rows, 1);
    commit_image(path, image, 1);
}

LoadedTable open_table(const std::string& path) {
    recover(path);
    const bool wal_present = std::filesystem::exists(wal_path(path)) &&
        std::filesystem::file_size(wal_path(path)) != 0;
    return parse_image(read_file(path), wal_present).table;
}

void insert_row(const std::string& path, const std::vector<std::string>& row) {
    recover(path);
    const auto next = build_incremental_insert(read_file(path), row);
    commit_image(path, next.image, next.generation, next.dirty_pages);
}

std::vector<std::uint64_t> query_index(
    const std::string& path,
    std::size_t field_index,
    const std::string& operation,
    const std::string& value
) {
    const auto table = open_table(path);
    if (field_index >= table.indexes.size()) throw std::invalid_argument("Unknown field index");
    const auto& index = table.indexes[field_index];
    std::vector<std::uint64_t> records;
    for (const auto& entry : index) {
        bool matches = false;
        if (operation == "=") matches = entry.first == value;
        else if (operation == "!=") matches = entry.first != value;
        else if (operation == "<") matches = entry.first < value;
        else if (operation == "<=") matches = entry.first <= value;
        else if (operation == ">") matches = entry.first > value;
        else if (operation == ">=") matches = entry.first >= value;
        else throw std::invalid_argument("Unsupported comparison operator: " + operation);
        if (matches) records.push_back(entry.second);
    }
    return records;
}

Inspection inspect(const std::string& path) {
    const auto path_wal = wal_path(path);
    if (std::filesystem::exists(path_wal) && std::filesystem::file_size(path_wal) != 0) {
        auto wal = parse_wal(read_file(path_wal));
        if (std::filesystem::exists(path)) {
            bool database_is_valid = false;
            std::uint64_t database_generation = 0;
            try {
                const auto database = parse_image(read_file(path), true);
                database_generation = database.table.inspection.generation;
                database_is_valid = true;
            } catch (const CorruptionError&) {
                // Inspection may report a valid WAL that can repair a torn database.
            }
            if (database_is_valid && database_generation > wal.generation) {
                throw CorruptionError("stale WAL generation would roll back the database");
            }
        }
        auto inspection = parse_image(wal.image, true).table.inspection;
        inspection.wal_present = true;
        return inspection;
    }
    return parse_image(read_file(path), false).table.inspection;
}

std::string describe(const std::string& path) {
    const auto table = inspect(path);
    std::ostringstream output;
    output << "path: " << path << '\n';
    output << "storage-format: " << table.format_version << '\n';
    output << "generation: " << table.generation << '\n';
    output << "page-size: " << PAGE_SIZE << '\n';
    output << "page-count: " << table.page_count << '\n';
    output << "field-count: " << table.field_count << '\n';
    output << "row-count: " << table.row_count << '\n';
    output << "index-roots:";
    for (const auto root : table.index_roots) {
        if (root == NO_PAGE) output << " none";
        else output << ' ' << root;
    }
    output << '\n';
    output << "wal: " << (table.wal_present ? "pending" : "clean") << '\n';
    return output.str();
}

} // namespace storage_v2
