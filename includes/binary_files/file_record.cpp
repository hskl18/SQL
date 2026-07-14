#include "file_record.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

FileRecord::FileRecord(vector<string> values) : _records(std::move(values)) {}

long FileRecord::write(fstream &outs) {
    if (_records.empty()) {
        throw std::invalid_argument("Cannot write an empty record");
    }
    outs.seekp(0, outs.end);
    long pos = outs.tellp();
    if (pos < 0) {
        throw std::runtime_error("Unable to seek to the end of the record file");
    }
    for (const auto& record : _records) {
        if (record.size() > MAX_VALUE_LENGTH) {
            throw std::length_error("Record value exceeds 100 bytes");
        }
        std::array<char, MAX> buffer{};
        std::copy(record.begin(), record.end(), buffer.begin());
        outs.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        if (!outs) {
            throw std::runtime_error("Unable to write a complete record");
        }
    }
    return pos / (MAX * this->_records.size());//return the record number
}
// Write the record to the end of the file

long FileRecord::read(istream &ins, long recno) {
    if (_records.empty() || recno < 0) {
        throw std::invalid_argument("Record shape and number must be valid");
    }
    long pos = recno * MAX * this->_records.size();
    ins.seekg(pos);
    if (!ins) return 0;
    long total = 0;

    for (auto & _record : this->_records){
        std::array<char, MAX> buffer{};
        ins.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto bytes = ins.gcount();
        if (bytes == 0 && total == 0) return 0;
        if (bytes != static_cast<std::streamsize>(buffer.size())) {
            throw std::runtime_error("Record file contains a partial record");
        }
        const auto end = std::find(buffer.begin(), buffer.end(), '\0');
        _record.assign(buffer.begin(), end);
        total += bytes;
    }
    return total;
    //return the number of bytes read
}
// Read a record from the file
