#include "parser.h"

//constructor
Parser::Parser(){
    this->tree = MMap<string, string>();
    this->input = vector<SToken>();
    this->types = vector<Ptype>();
    this->token = STokenizer();
    this->init_();
}
Parser::Parser(const string& str): Parser(){
    this->token.set_string(str.c_str());
    this->tokenize();
    this->init_();
}

// save string to private member
void Parser::set_string(const char*& cstr){
    this->tree.clear();
    this->types.clear();
    this->input.clear();
    this->token.set_string(cstr);
    this->tokenize();
}
void Parser::set_string(const string& str){
    this->tree.clear();
    this->types.clear();
    this->input.clear();

    this->token.set_string(str.c_str());
    this->tokenize();
}

// parse string to map
MMap<string, string> Parser::parse_tree(){
    bool success = this->parse();
    if (!success){this->tree.clear();}
    return this->tree;
}

bool Parser::parse(){
    // Check if the number of types matches the number of input tokens
    if (this->types.empty() || this->types.size() != this->input.size()) return false;

    int state = 0;
    Ptype type = this->types[0];
    bool vali = false;
    bool did_where = false;

    // Determine the command type and initialize tree keys
    if (type == CREATE){
        this->tree.insert("command", "create");
        this->tree.insert_key("fields");
        this->tree.insert_key("table_name");
        vali = true;
    }
    if (type == INSERT){
        this->tree.insert("command", "insert");
        this->tree.insert_key("values");
        this->tree.insert_key("table_name");
        vali = true;
    }
    if (type == SELECT){
        this->tree.insert("command", "select");
        this->tree.insert_key("condition");
        this->tree.insert_key("fields");
        this->tree.insert_key("table_name");
        this->tree.insert_key("where");
        vali = true;
    }

    // If the command is not valid, return false
    if (!vali) return false;

    // Update the state based on the state table
    state = this->table[0][type];

    for (std::size_t i = 1; i < this->input.size(); ++i){
        type = this->types[i];
        int prev_state = state;
        state = this->table[prev_state][type];
        if (state == -1) return false;

        // Add literals to the corresponding tree nodes
        if (state == 3 || state == 9 || state == 18) this->tree["table_name"] += this->input[i].token_str(); // table name in state 3 for create, and state 9 for insert
        if (state == 5 || state == 16 || state == 14) this->tree["fields"] += this->input[i].token_str();
        if (state == 11) this->tree["values"] += this->input[i].token_str();
        if (state == 19 && !did_where){
            did_where = true;
            this->tree["where"] += string("yes");
        }
        if (state >= 19 && state <= 24 && this->input[i].token_str() != "where") this->tree["condition"] += this->input[i].token_str();
    }

    // Remove empty nodes from the tree
    if (this->tree["condition"].empty()) this->tree.erase("condition");
    if (this->tree["where"].empty()) this->tree.erase("where");

    // Return true if the state is successful, otherwise return false
    return is_success(this->table, state);
}


// Initialize state machine for SQL parser
void Parser::init_(){
    init_table(this->table);

    // Mark success and fail states
    for (int i = 0; i <= 24; ++i){
        if (i == 5 || i == 11 || i == 18 || i == 23 || i == 24)mark_success(this->table, i);
        else mark_fail(this->table, i);
    }

    // CREATE TABLE statement
    mark_cell(0, this->table, CREATE, 1);
    mark_cell(1, this->table, TABLE, 2);
    mark_cell(2, this->table, LITERAL, 3);
    mark_cell(3, this->table, FIELDS, 4);
    mark_cell(4, this->table, LITERAL, 5);
    mark_cell(5, this->table, COMMAS, 6);
    mark_cell(6, this->table, LITERAL, 5);

    // INSERT INTO statement
    mark_cell(0, this->table, INSERT, 7);
    mark_cell(7, this->table, INTO, 8);
    mark_cell(8, this->table, LITERAL, 9);
    mark_cell(9, this->table, VALUES, 10);
    mark_cell(10, this->table, LITERAL, 11);
    mark_cell(11, this->table, COMMAS, 12);
    mark_cell(12, this->table, LITERAL, 11);

    // SELECT statement
    mark_cell(0, this->table, SELECT, 13);
    mark_cell(13, this->table, ASTERISK, 16);
    mark_cell(16, this->table, FROM, 17);
    mark_cell(13, this->table, LITERAL, 14);
    mark_cell(14, this->table, COMMAS, 15);
    mark_cell(15, this->table, LITERAL, 14);
    mark_cell(14, this->table, FROM, 17);
    mark_cell(17, this->table, LITERAL, 18);
    mark_cell(18, this->table, WHERE, 19);
    mark_cell(19, this->table, LITERAL, 21);
    mark_cell(19, this->table, PARENS, 20);
    mark_cell(20, this->table, LITERAL, 21);
    mark_cell(20, this->table, PARENS, 20);
    mark_cell(21, this->table, RELATIONAL, 22);
    mark_cell(22, this->table, LITERAL, 23);
    mark_cell(23, this->table, LOGICAL, 19);
    mark_cell(23, this->table, PARENS, 24);
    mark_cell(24, this->table, LOGICAL, 19);
    mark_cell(24, this->table, PARENS, 24);
}


// tokenize string into vector
void Parser::tokenize(){
    while (this->token.more()){
        SToken token = SToken();
        this->token >> token;
        if (token.type() == TOKEN_UNKNOWN && token.token_str() != "\"") continue;
        if (token.type() == TOKEN_SPACE) continue;
        // reserved keywords
        bool did_push = false;
        if (token.type() == TOKEN_ALPHA){
            std::size_t prev_size = this->types.size();
            if (token.token_str() == "create") this->types.push_back(CREATE);
            if (token.token_str() == "table") this->types.push_back(TABLE);
            if (token.token_str() == "fields") this->types.push_back(FIELDS);
            if (token.token_str() == "insert") this->types.push_back(INSERT);
            if (token.token_str() == "into") this->types.push_back(INTO);
            if (token.token_str() == "values") this->types.push_back(VALUES);
            if (token.token_str() == "select") this->types.push_back(SELECT);
            if (token.token_str() == "from") this->types.push_back(FROM);
            if (token.token_str() == "where") this->types.push_back(WHERE);

            if (prev_size != this->types.size()) did_push = true;
        }

        // logical and relational
        if (token.type() == TOKEN_ALPHA && (token.token_str() == "or" || token.token_str() == "and")){
            did_push = true;
            this->types.push_back(LOGICAL);
        }
        // =, !=, <, >, <= and >=
        bool is_relational = (token.token_str() == "<=" || token.token_str() == ">=" || token.token_str() == "=" || token.token_str() == ">" || token.token_str() == "<" || token.token_str() == "!=");
        if (token.type() == TOKEN_OPERATOR && is_relational) this->types.push_back(RELATIONAL);

        if (token.type() == TOKEN_PAREN) this->types.push_back(PARENS);
        if (token.type() == TOKEN_STAR && token.token_str() == "*") this->types.push_back(ASTERISK);
        if (token.type() == TOKEN_COMMA && token.token_str() == ",") this->types.push_back(COMMAS);
        if (!did_push && (token.type() == TOKEN_ALPHA || token.type() == TOKEN_NUMBER)) this->types.push_back(LITERAL);
        if (token.type() == TOKEN_UNKNOWN && token.token_str() == "\"") this->types.push_back(QUOTE);
        this->input.push_back(token);
    }

    // process "" here, basically concat
    vector<SToken> clean_input;
    vector<Ptype> clean_type;
    for (std::size_t i = 0; i < this->input.size(); ++i){
        if (this->input[i].token_str() == "\""){
            std::size_t j = i + 1;
            string s = "";
            while (this->input[j].type() == TOKEN_ALPHA){
                if (j != i + 1) s += " ";
                s += this->input[j].token_str();
                ++j;
            }
            i = j;
            clean_input.emplace_back(s, TOKEN_ALPHA);
            clean_type.push_back(LITERAL);
            continue;
        }
        clean_input.push_back(this->input[i]);
        clean_type.push_back(this->types[i]);
    }

    this->types = clean_type;
    this->input = clean_input;
}


vector<SToken> Parser::get_input() {
    return this->input;
}
