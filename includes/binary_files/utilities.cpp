#include "utilities.h"

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
        throw "File could not be opened";
    }
}
// Open a file for writing

void open_fileW(fstream& f, const char filename[]){
    f.open(filename, ios::out | ios::binary);
    if(!f.is_open()){
        throw "File could not be opened";
    }
}
// Open a file for reading
