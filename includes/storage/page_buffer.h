#ifndef STORAGE_PAGE_BUFFER_H
#define STORAGE_PAGE_BUFFER_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <set>
#include <vector>

namespace storage_v2 {

class PageBuffer {
public:
    static constexpr std::size_t PAGE_BYTES = 4096;
    using Visitor = std::function<void(std::uint32_t, const std::uint8_t*)>;

    explicit PageBuffer(
        std::vector<std::uint8_t> image,
        std::set<std::uint32_t> dirty_pages = {}
    );

    std::size_t page_count() const;
    const std::uint8_t* page_data(std::uint32_t page_id) const;
    const std::vector<std::uint8_t>& bytes() const { return image_; }
    void visit_pages(const Visitor& visitor) const;
    void visit_dirty_pages(const Visitor& visitor) const;
    const std::set<std::uint32_t>& dirty_pages() const { return dirty_pages_; }

private:
    std::vector<std::uint8_t> image_;
    std::set<std::uint32_t> dirty_pages_;
};

} // namespace storage_v2

#endif
