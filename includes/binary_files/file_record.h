#ifndef FILE_RECORD_H
#define FILE_RECORD_H

#include <iostream>  // cout, endl
#include <iomanip>   // setw, right
#include <fstream>   // fstream
#include <vector>    // vector
#include <cstddef>

using namespace std;

class FileRecord{
public:
    static constexpr std::size_t MAX_VALUE_LENGTH = 100;
    // When construct a FileRecord, it's either empty or it contains a word
    FileRecord(){}
    explicit FileRecord(vector<string> values);

    long write(fstream& outs);
    long read(istream& ins, long recno);
    std::size_t encoded_size() const { return MAX_VALUE_LENGTH * _records.size(); }
    vector<string> get_records_string(){ return this->_records;}

    int column_size() { return this->_records.size(); }
    // Overload the << operator to print a FileRecord
    void resize(int size) { this->_records.resize(size); }

    friend ostream& operator << (ostream& outs, const FileRecord& r) {
        for (const auto& record : r._records) {
            if (record.empty()) break;
            outs << setw(MAX / 4) << right << record;
        }
        return outs;
        // return outs << setw(MAX / 4) << right << r._records[0] << setw(MAX / 4) << right << r._records[1] << setw(MAX / 4) << right << r._records[2];
    }

private:
    // The maximum size of the record
    static const int MAX = static_cast<int>(MAX_VALUE_LENGTH);
    // The record vector
    vector<string> _records;

};

#endif
