#include "utilities.h"

#include <stdexcept>

bool file_exists(const char filename[]){
    fstream f;
    f.open(filename, fstream::in | fstream::binary);
    if(f.is_open()){
        f.close();
        return true;
    }
    return false;
}
// Open a file for reading and writing

void open_fileRW(fstream& f, const char filename[]){
    f.open(filename, ios::in | ios::out | ios::binary);
    if(!f.is_open()){
        throw std::runtime_error(std::string("File could not be opened: ") + filename);
    }
}
// Open a file for writing

void open_fileW(fstream& f, const char filename[]){
    f.open(filename, ios::out | ios::binary);
    if(!f.is_open()){
        throw std::runtime_error(std::string("File could not be opened: ") + filename);
    }
}
// Open a file for reading
