#include "state_machine_functions.h"

//Fill all cells of the array with -1
void init_table(int _table[][MAX_COLUMNS]){
    for(int i = 0; i < MAX_ROWS; i++){
        for(int j = 0; j < MAX_COLUMNS; j++){
            _table[i][j] = -1;
        }
    }
}

//Mark this state (row) with a 1 (success)
void mark_success(int _table[][MAX_COLUMNS], int state){
    _table[state][0] = 1;
}

//Mark this state (row) with a 0 (fail)
void mark_fail(int _table[][MAX_COLUMNS], int state){
    _table[state][0] = 0;
}

//true if state is a success state
bool is_success(int _table[][MAX_COLUMNS], int state){
    return _table[state][0] == 1;
}

//Mark a range of cells in the array.
void mark_cells(int row, int _table[][MAX_COLUMNS], int from, int to, int state){
    for(int i = from; i <= to; i++){
        _table[row][i] = state;
    }
}

//Mark columns represented by the string columns[] for this row
void mark_cells(int row, int _table[][MAX_COLUMNS], const char columns[], int state){
    for(std::size_t i = 0; i < strlen(columns); i++){
        const auto column = static_cast<unsigned char>(columns[i]);
        _table[row][column] = state;
    }
}

//Mark this row and column
void mark_cell(int row, int table[][MAX_COLUMNS], int column, int state){
    table[row][column] = state;
}

//This can realistically be used on a small table
void print_table(int _table[][MAX_COLUMNS]){
    for(int i = 0; i < MAX_ROWS; i++){
        for(int j = 0; j < MAX_COLUMNS; j++){
            std::cout << std::setw(2) << _table[i][j];
        }
        std::cout << std::endl;
    }
}

//show string s and mark this position on the string:
//hello world   pos: 7
//       ^
void show_string(char s[], int _pos){
    std::cout << s << " pos: " << _pos << std::endl;
    for(int i = 0; i < _pos; i++){
        std::cout << " ";
    }
    std::cout << "^" << std::endl;
}
