#include "page_buffer.h"

#include <limits>
#include <utility>

namespace storage_v2 {

PageBuffer::PageBuffer(
    std::vector<std::uint8_t> image,
    std::set<std::uint32_t> dirty_pages
)
    : image_(std::move(image)), dirty_pages_(std::move(dirty_pages)) {
    if (image_.empty() || image_.size() % PAGE_BYTES != 0) {
        throw std::invalid_argument("Page buffer requires a non-empty complete page image");
    }
    if (page_count() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("Page buffer contains too many pages");
    }
    if (dirty_pages_.empty()) {
        for (std::uint32_t page = 0; page < page_count(); ++page) dirty_pages_.insert(page);
    }
    if (*dirty_pages_.rbegin() >= page_count()) {
        throw std::out_of_range("Dirty page is outside the owned image");
    }
}

std::size_t PageBuffer::page_count() const {
    return image_.size() / PAGE_BYTES;
}

const std::uint8_t* PageBuffer::page_data(std::uint32_t page_id) const {
    if (page_id >= page_count()) {
        throw std::out_of_range("Page buffer read is outside the owned image");
    }
    return image_.data() + static_cast<std::size_t>(page_id) * PAGE_BYTES;
}

void PageBuffer::visit_pages(const Visitor& visitor) const {
    for (std::uint32_t page_id = 0; page_id < page_count(); ++page_id) {
        visitor(page_id, page_data(page_id));
    }
}

void PageBuffer::visit_dirty_pages(const Visitor& visitor) const {
    for (const auto page_id : dirty_pages_) visitor(page_id, page_data(page_id));
}

} // namespace storage_v2
