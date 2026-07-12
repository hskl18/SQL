#include "sql.h"

// Constructor
SQL::SQL(){
    this->_table=Table();
    this->_parser=Parser();
    this->_error=false;
}
// Process commands from a file
SQL::SQL(const char* file): SQL(){this->batch(file);}

// Process a command and return the result table
Table SQL::command(const string& cmd){

    this->_parser.set_string(cmd);
    MMap<string, string> parsed_tree = this->_parser.parse_tree();
    this->_ptree = parsed_tree;

    this->_error = _ptree.empty();
    if (this->_error) return _table;

    string table_name = parsed_tree["table_name"][0];
    string command = parsed_tree["command"][0];

    if (command == "create"){
        vector<string> fields = parsed_tree["fields"];
        _table = Table(table_name, fields);

    }else if (command == "insert"){
        _table = Table(table_name);
        _table.insert_into(parsed_tree["values"]);

    }else if (command == "select"){
      
        bool where_exists = parsed_tree.contains("where");
        // update for select *
        vector<string> selected_fields = parsed_tree["fields"];
        Table t = Table(table_name);
        if (selected_fields[0] == "*") selected_fields = t.get_fields();

        // basic select
        if (!where_exists){_table = t.select(selected_fields);
        }else{_table = t.select(selected_fields, parsed_tree["condition"]);}

    }
    return _table;

}


void SQL::batch(const char* file){
    ifstream f;
    f.open(file);

    if (f.fail()){
        this->_error = true;
        cout << "No file named " << file << " exists." << endl;
        return;
    }

    this->_error = false;
    cout << "------------------------------Batch Begins------------------------------" << endl;
    string str;
    while (getline(f, str)){

        if(!str.empty() && str[0] != '/'){
            cout << "command:" << str << endl;
            this->_table = this->command(str);

            cout << this->_table << endl;
            //this._table.print_lookup();
            cout << "records selected: " << select_recnos() << endl;
            cout << endl;
        }
    }

    f.close();
    cout << "------------------------------DONE------------------------------" << endl;

}
