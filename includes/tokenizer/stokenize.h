#ifndef STOKENIZE_H
#define STOKENIZE_H

#include <cassert>
#include <cstring>
#include <iostream>

#include "constants.h"
#include "state_machine_functions.h"
#include "token.h"

class STokenizer {
private:
    string _buffer;
    std::size_t _pos{};
    int _table[MAX_ROWS][MAX_COLUMNS]{};

    void make_table();
    STRING_TOKEN_TYPES token_type(int state) const;
    bool get_token(int start_state, SToken& token);

public:
    STokenizer();
    STokenizer(const char str[]);
    void set_string(const char str[]);

    bool done();
    bool more();
    friend STokenizer& operator>>(STokenizer& s, SToken& t);
};

#endif // STOKENIZE_H
