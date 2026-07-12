#include "file_record.h"

//Constructors
FileRecord::FileRecord(string){}
FileRecord::FileRecord(char *str){
    int n = strlen(str);
    this->_records.emplace_back(str,n);
}
FileRecord::FileRecord(vector<string> v) :FileRecord() {
    this->_records=v;
}

long FileRecord::write(fstream &outs) {
    outs.seekg(0, outs.end);
    long pos = outs.tellp();
    for (const auto & _record : this->_records) outs.write(_record.c_str(), MAX);
    return pos / (MAX * this->_records.size());//return the record number
}
// Write the record to the end of the file

long FileRecord::read(fstream &ins, long recno) {
    long pos = recno * MAX * this->_records.size();
    ins.seekg(pos);
    long total = 0;

    for (auto & _record : this->_records){
        char temp[MAX + 1];
        ins.read(temp, MAX);
        temp[ins.gcount()] = '\0';

        if (ins.gcount() == 0) return 0;

        _record = temp;
        total += ins.gcount();
    }
    return total;
    //return the number of bytes read
}
// Read a record from the file

vector<char *> FileRecord::get_records() {
    vector<char*> ans;
    for (auto record : this->_records){
        char* temp = new char[record.size() + 1];
        strncpy(temp, record.c_str(), record.size());

        temp[record.size()] = '\0';
        ans.push_back(temp);
    }
    return ans;
    //return the record
}
