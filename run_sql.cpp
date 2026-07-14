#include "includes/sql/sql.h"
#include "includes/storage/storage_v2.h"
#include "version.h"

#include <exception>

using namespace std;

void displayInstructions() {
    cout << "Commands: \n";
    cout << "Type \"end\" to end.\n";
    cout << "Type \"cls\" to clear the screen.\n";
}

void clearScreen() {
#ifdef _WIN32
    const int result = system("cls");
    if (result != 0) cerr << "Unable to clear the terminal." << endl;
#else
    cout << "\033[2J\033[H" << flush;
#endif
}

int main(int argc, char* argv[]) {
    if (argc == 2 && string(argv[1]) == "--version") {
        cout << "SQL in C++ " << SQL_IN_CPP_VERSION
             << " (storage format " << storage_v2::FORMAT_VERSION << ")" << endl;
        return 0;
    }
    if (argc == 3 && string(argv[1]) == "--inspect-storage") {
        try {
            cout << storage_v2::describe(storage_v2::database_path(argv[2]));
            return 0;
        } catch (const exception& error) {
            cerr << "Error: " << error.what() << endl;
            return 1;
        }
    }
    if (argc != 1) {
        cerr << "Usage: run_sql [--version | --inspect-storage <table>]" << endl;
        return 2;
    }
    SQL sql;
    displayInstructions();
    while (true) {
        string input;
        cout << "SQL>> ";
        getline(cin, input);

        if (input.empty()) {
            continue;
        }else if (input == "cls") {
            clearScreen();
            continue;
        }else if (input == "end") {
            break;
        }

        Table tb = sql.command(input);

        if (sql.is_error()) {
            cout << "Error: " << sql.last_error() << endl;
            continue;
        }

        if (tb.get_fields().empty()) {
            cout << "Empty table." << endl;
            continue;
        }else{
            cout << "Selected fields: " << tb.get_fields();
            cout << tb << endl;
            cout << "Record count: " << sql.select_recnos() << endl;
            cout << "-------------------------------------------------------------------------------------------------------------------------"
                 << endl;}
    }

    cout << "DONE" << endl;
    return 0;
}
//try
//> create table employee fields  last,       first,         dep,      salary, year
//> insert into employee values Blow,       Joe,           CS,       100000, 2018
//> insert into employee values Blow,       JoAnn,         Physics,  200000, 2016
//> insert into employee values Johnson,    Jack,          HR,       150000, 2014
//> insert into employee values Johnson,    "Jimmy",     Chemistry,140000, 2018

//> select * from employee
//> select last, first, age from employee
//> select last from employee
//> select * from employee where last = Johnson
//> select * from employee where last=Blow and major="JoAnn"

//> create table student fields  fname,          lname,    major,    age
//> insert into student values Flo,            Yao,       Art,    20
//> insert into student values Bo,                     Yang,      CS,             28
//> insert into student values "Sammuel L.", Jackson,     CS,             40
//> insert into student values "Billy",        Jackson,   Math,   27
//> insert into student values "Mary Ann",   Davis,       Math,   30

//> select * from student
//> select * from student where (major=CS or major=Art)
//> select * from student where lname>J
//> select * from student where lname>J and (major=CS or major=Art)
