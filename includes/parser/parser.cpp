#include "parser.h"

namespace {

bool classify_unquoted_token(const SToken& token, Ptype& type) {
    const string text = token.token_str();

    if (token.type() == TOKEN_ALPHA) {
        if (text == "create") type = CREATE;
        else if (text == "table") type = TABLE;
        else if (text == "fields") type = FIELDS;
        else if (text == "insert") type = INSERT;
        else if (text == "into") type = INTO;
        else if (text == "values") type = VALUES;
        else if (text == "select") type = SELECT;
        else if (text == "from") type = FROM;
        else if (text == "where") type = WHERE;
        else if (text == "or" || text == "and") type = LOGICAL;
        else type = LITERAL;
        return true;
    }

    if (token.type() == TOKEN_NUMBER) {
        type = LITERAL;
        return true;
    }

    if (token.type() == TOKEN_OPERATOR) {
        const bool is_relational =
            text == "<=" || text == ">=" || text == "=" ||
            text == ">" || text == "<" || text == "!=";
        if (!is_relational) return false;
        type = RELATIONAL;
        return true;
    }

    if (token.type() == TOKEN_PAREN) {
        type = PARENS;
        return true;
    }
    if (token.type() == TOKEN_STAR && text == "*") {
        type = ASTERISK;
        return true;
    }
    if (token.type() == TOKEN_COMMA && text == ",") {
        type = COMMAS;
        return true;
    }

    return false;
}

} // namespace

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
    bool inside_quote = false;
    string quoted_literal;

    while (this->token.more()){
        SToken current;
        this->token >> current;
        const string text = current.token_str();

        if (inside_quote) {
            if (text == "\"") {
                this->input.emplace_back(quoted_literal, TOKEN_ALPHA);
                this->types.push_back(LITERAL);
                quoted_literal.clear();
                inside_quote = false;
            } else {
                quoted_literal += text;
            }
            continue;
        }

        if (text == "\"") {
            inside_quote = true;
            continue;
        }
        if (current.type() == TOKEN_SPACE) continue;

        Ptype type = LITERAL;
        if (!classify_unquoted_token(current, type)) {
            this->input.clear();
            this->types.clear();
            return;
        }

        this->input.push_back(current);
        this->types.push_back(type);
    }

    if (inside_quote) {
        this->input.clear();
        this->types.clear();
    }
}


vector<SToken> Parser::get_input() {
    return this->input;
}
