#include "sql.h"

#include <cctype>
#include <stdexcept>

namespace {

bool is_identifier(const string& value) {
    if (value.empty()) return false;
    const auto first = static_cast<unsigned char>(value.front());
    if (!(std::isalpha(first) || value.front() == '_')) return false;
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (!(std::isalnum(byte) || character == '_')) return false;
    }
    return true;
}

void require_identifier(const string& value, const string& label) {
    if (!is_identifier(value)) {
        throw std::invalid_argument(label + " must be a valid identifier");
    }
}

} // namespace

// Constructor
SQL::SQL(){
    this->_table=Table();
    this->_parser=Parser();
    this->_error=false;
    this->_last_error="";
}
// Process commands from a file
SQL::SQL(const char* file): SQL(){this->batch(file);}

// Process a command and return the result table
Table SQL::command(const string& cmd){
    _error = false;
    _last_error.clear();
    try {
        _parser.set_string(cmd);
        MMap<string, string> parsed_tree = _parser.parse_tree();
        _ptree = parsed_tree;
        if (_ptree.empty()) {
            throw std::invalid_argument("Invalid command syntax");
        }

        const string table_name = parsed_tree["table_name"][0];
        const string command_name = parsed_tree["command"][0];
        require_identifier(table_name, "Table name");

        if (command_name == "create") {
            const vector<string> fields = parsed_tree["fields"];
            for (const auto& field : fields) require_identifier(field, "Field name");
            _table = Table(table_name, fields);
        } else if (command_name == "insert") {
            _table = Table(table_name);
            _table.insert_into(parsed_tree["values"]);
        } else if (command_name == "select") {
            const bool where_exists = parsed_tree.contains("where");
            vector<string> selected_fields = parsed_tree["fields"];
            Table table(table_name);
            if (selected_fields[0] == "*") selected_fields = table.get_fields();
            if (!where_exists) {
                _table = table.select(selected_fields);
            } else {
                _table = table.select(selected_fields, parsed_tree["condition"]);
            }
        } else {
            throw std::invalid_argument("Unsupported command");
        }
    } catch (const std::exception& error) {
        _error = true;
        _last_error = error.what();
        _table = Table();
    }
    return _table;
}


void SQL::batch(const char* file){
    ifstream f;
    f.open(file);

    if (f.fail()){
        this->_error = true;
        this->_last_error = string("Batch file does not exist: ") + file;
        cout << "No file named " << file << " exists." << endl;
        return;
    }

    this->_error = false;
    this->_last_error.clear();
    cout << "------------------------------Batch Begins------------------------------" << endl;
    string str;
    while (getline(f, str)){

        if(!str.empty() && str[0] != '/'){
            cout << "command:" << str << endl;
            this->_table = this->command(str);

            if (this->_error) {
                cout << "Error: " << this->_last_error << endl << endl;
                continue;
            }

            cout << this->_table << endl;
            //this._table.print_lookup();
            cout << "records selected: " << select_recnos() << endl;
            cout << endl;
        }
    }

    f.close();
    cout << "------------------------------DONE------------------------------" << endl;

}
