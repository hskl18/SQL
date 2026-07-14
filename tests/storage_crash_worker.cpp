#include "includes/storage/storage_v2.h"

#include <exception>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc != 3) return 2;
    try {
        if (std::string(argv[2]) == "__create__") {
            storage_v2::create_table(argv[1], {"value"});
        } else {
            storage_v2::insert_row(argv[1], {argv[2]});
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
