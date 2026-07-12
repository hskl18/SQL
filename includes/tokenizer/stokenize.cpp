#include "stokenize.h"

STokenizer::STokenizer() {
    _buffer = "";
    _pos = 0;
    make_table();
}

STokenizer::STokenizer(const char str[]) {
    set_string(str);
    make_table();
}

void STokenizer::set_string(const char str[]) {
    _pos = 0;
    _buffer = str;
}

bool STokenizer::done() {
    return _pos >= _buffer.length();
}

bool STokenizer::more() {
    return !done();
}

void STokenizer::make_table() {
    init_table(_table);
    mark_fail(_table, 0);
    for (int i = 1; i <= 7; ++i) mark_success(_table, i);

    mark_cells(0, _table, ALPHAS, 1);
    mark_cells(0, _table, DIGITS, 2);
    mark_cells(0, _table, OPERATORS, 3);
    mark_cells(0, _table, COMMA, 4);
    mark_cells(0, _table, SPACES, 5);
    mark_cells(0, _table, PAREN, 6);
    mark_cells(0, _table, STAR, 7);

    mark_cells(1, _table, ALPHAS, 1);
    mark_cells(2, _table, DIGITS, 2);
    mark_cells(3, _table, OPERATORS, 3);
}

STRING_TOKEN_TYPES STokenizer::token_type(int state) const {
    switch (state) {
        case 1:
            return TOKEN_ALPHA;
        case 2:
            return TOKEN_NUMBER;
        case 3:
            return TOKEN_OPERATOR;
        case 4:
            return TOKEN_COMMA;
        case 5:
            return TOKEN_SPACE;
        case 6:
            return TOKEN_PAREN;
        case 7:
            return TOKEN_STAR;
        default:
            return TOKEN_UNKNOWN;
    }
}

bool STokenizer::get_token(int start_state, SToken& token) {
    if (done()) return false;

    std::size_t start = _pos;
    std::size_t hold = start;
    unsigned char t = static_cast<unsigned char>(_buffer[_pos]);

    int state = _table[start_state][t];

    if (state >= 4 && state <= 7) {
        token = SToken(string(1, static_cast<char>(t)), token_type(state));
        _pos++;
        return true;
    }

    if (state == -1) {
        token = SToken(string(1, static_cast<char>(t)), TOKEN_UNKNOWN);
        _pos++;
        return true;
    }

    std::size_t size = _buffer.length();
    start++;

    while (start < size) {
        t = static_cast<unsigned char>(_buffer[start]);
        int next_state = _table[state][t];

        if (state == 1) {
            if (next_state == 1) {
                start++;
                continue;
            }
            break;
        }else if (state == 2) {
            if (next_state == 2) {
                start++;
                continue;
            }
            if (t == '.' && start + 1 < size) {
                unsigned char next_token =
                    static_cast<unsigned char>(_buffer[start + 1]);
                while (start < size) {
                    int next_next_state = _table[state][next_token];
                    if (next_next_state != 2) break;
                    start++;
                    next_token = static_cast<unsigned char>(_buffer[start]);
                }
                break;
            }
            break;
        }else if (state == 3) {
            if (next_state == 3) {
                start++;
                continue;
            }
            break;
        }else if (state == 4) {
            if (next_state == 4) {
                start++;
                continue;
            }
            break;
        }
        break;
    }

    string s = _buffer.substr(hold, start - hold);
    _pos = start;
    token = SToken(s, token_type(state));
    return true;
}

STokenizer &operator>>(STokenizer &s, SToken &t) {
    s.get_token(0, t);
    return s;
}
