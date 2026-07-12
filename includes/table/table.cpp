#include "table.h"

#include <filesystem>
#include <limits>
#include <stdexcept>

// Default constructor for SELECT queries
Table::Table() {
    numRecords = 0;
    tableName = "";
}

// Constructor for CREATE TABLE queries
string Table::create_table(const string& table_name, const vector<string>& field_names) {
    *this = Table(table_name, field_names);
    return "create " + table_name + " success";
}


// Constructor for table creation
Table::Table(const string& table_name, const vector<string>& field_names) : Table() {
    string table_file = table_name + ".bin";
    string field_file = table_name + "_fields.bin";
    if (file_exists(table_file.c_str()) || file_exists(field_file.c_str())) {
        throw std::runtime_error("Table already exists: " + table_name);
    }
    if (field_names.empty() || field_names.size() > MAX_FIELDS) {
        throw std::invalid_argument("A table must have between 1 and 64 fields");
    }
    std::set<string> unique_fields;
    for (const auto& field : field_names) {
        if (field.empty() || field.size() > FileRecord::MAX_VALUE_LENGTH) {
            throw std::invalid_argument("Field names must be between 1 and 100 bytes");
        }
        if (!unique_fields.insert(field).second) {
            throw std::invalid_argument("Duplicate field name: " + field);
        }
    }
    tableName = table_name;
    fieldNames = field_names;
    selectedFields = field_names;

    fstream f;

    // Record the number of fields
    open_fileW(f, field_file.c_str());
    vector<string> mark;
    int size = fieldNames.size();
    string mark_size = to_string(size);
    mark.push_back(mark_size);
    for (const auto & fieldName : fieldNames) {
        mark.push_back(fieldName);
    }
    FileRecord r(mark);
    r.write(f);
    f.close();

    // Create record file
    open_fileW(f, table_file.c_str());
    f.close();
    recordIndices.clear();

    // Initialize fieldNameMap and cache
    for (std::size_t i = 0; i < fieldNames.size(); ++i) {
        fieldNameMap[fieldNames[i]] = i;
        cache[fieldNames[i]] = MMap<string, long>();
    }
}

// Constructor for existing table
Table::Table(const string& table_name) : Table() {
    string table_file = table_name + ".bin";
    string field_file = table_name + "_fields.bin";
    if (!file_exists(table_file.c_str()) || !file_exists(field_file.c_str())) {
        throw std::runtime_error("Table does not exist: " + table_name);
    }
    tableName = table_name;

    fstream f;
    FileRecord r;

    // Determine the fields
    open_fileRW(f, field_file.c_str());
    r.resize(1);
    if (r.read(f, 0) != static_cast<long>(r.encoded_size())) {
        throw std::runtime_error("Field metadata is incomplete for table: " + table_name);
    }
    const string raw_size = r.get_records_string()[0];
    std::size_t parsed = 0;
    unsigned long raw_count = 0;
    try {
        raw_count = std::stoul(raw_size, &parsed);
    } catch (const std::exception&) {
        throw std::runtime_error("Field metadata is invalid for table: " + table_name);
    }
    if (parsed != raw_size.size() || raw_count < 1 || raw_count > MAX_FIELDS) {
        throw std::runtime_error("Field count is invalid for table: " + table_name);
    }
    const auto size = static_cast<std::size_t>(raw_count);
    r.resize(size + 1);
    if (std::filesystem::file_size(field_file) != r.encoded_size()) {
        throw std::runtime_error("Field metadata size is invalid for table: " + table_name);
    }

    // Read the field names
    f.clear();
    if (r.read(f, 0) != static_cast<long>(r.encoded_size())) {
        throw std::runtime_error("Field metadata is incomplete for table: " + table_name);
    }
    vector<string> field_names = r.get_records_string();
    auto it = field_names.begin();
    // Exclude the size mark
    field_names.erase(it);

    fieldNames = field_names;
    selectedFields = field_names;
    f.close();

    std::set<string> unique_fields;
    for (const auto& field : fieldNames) {
        if (field.empty() || !unique_fields.insert(field).second) {
            throw std::runtime_error("Field metadata contains invalid names for table: " + table_name);
        }
    }

    for (std::size_t i = 0; i < fieldNames.size(); ++i) {
        fieldNameMap[fieldNames[i]] = i;
    }

    // Resize the vector
    r.resize(fieldNames.size());
    const auto file_bytes = std::filesystem::file_size(table_file);
    if (file_bytes % r.encoded_size() != 0) {
        throw std::runtime_error("Record file contains a partial row for table: " + table_name);
    }
    open_fileRW(f, table_file.c_str());

    // Read every record
    for (long i = 0;; ++i) {
        const long bytes = r.read(f, i);
        if (bytes == 0) break;
        // Increase the number of records
        ++numRecords;

        // Put the ith_entry into cache
        vector<string> ith_entry = r.get_records_string();
        for (std::size_t ith_entry_walker = 0; ith_entry_walker < ith_entry.size(); ++ith_entry_walker) {
            string field_value = ith_entry[ith_entry_walker];
            long index = i;
            cache[fieldNames[ith_entry_walker]].insert(field_value, index);
        }

        // Update recordIndices and printQueue
        recordIndices.push_back(i);
        printQueue += ith_entry;
    }
    f.close();
}

Table& Table::operator=(const Table& RHS){
    if (this == &RHS) return *this;
    tableName = RHS.tableName;
    cache = RHS.cache;
    recordIndices = RHS.recordIndices;
    printQueue = RHS.printQueue;
    fieldNames = RHS.fieldNames;
    selectedFields = RHS.selectedFields;
    fieldNameMap = RHS.fieldNameMap;
    numRecords = RHS.numRecords;
    return *this;
}


// Function to insert data into the table
string Table::insert_into(const vector<string>& field_values) {
    if (field_values.size() != fieldNames.size()) {
        throw std::invalid_argument(
            "Expected " + std::to_string(fieldNames.size()) +
            " values, received " + std::to_string(field_values.size())
        );
    }
    for (const auto& value : field_values) {
        if (value.size() > FileRecord::MAX_VALUE_LENGTH) {
            throw std::length_error("Values must not exceed 100 bytes");
        }
    }
    // Insert data into binary file
    fstream f;
    string table_file = tableName + ".bin";

    // Open the file in read/write mode
    open_fileRW(f, table_file.c_str());
    // Create a new FileRecord with the given field_values
    FileRecord r(field_values);
    // Write the record to the binary file and obtain its index
    long index = r.write(f);

    // Close the file
    f.close();

    // Insert the record into the cache
    for (std::size_t i = 0; i < field_values.size(); ++i) {
        // Extract the field value and field name
        const string& field_value = field_values[i];
        string field_name = fieldNames[i];

        // Insert the record index into the cache's MMap for the field
        cache[field_name].insert(field_value, index);
    }

    // Update recordIndices to keep track of the inserted record's index
    recordIndices.push_back(index);
    // Add field_values to the printQueue for printing
    printQueue += field_values;
    // Increment the number of records in the table
    numRecords++;
    return "insert success";
}


// print
ostream& operator<<(ostream& outs, const Table& print_me){
    // for field names
    outs << setw(20) << right << "record";
    for (const auto & fieldName : print_me.fieldNames) outs << setw(20) << right << fieldName;

    // for field values
    int eIndex = print_me.fieldNames.size();
    for (std::size_t i = 0; i < print_me.printQueue.size(); ++i){
        string field_value = print_me.printQueue[i];
        if (i % eIndex == 0){
            outs << endl;
            outs << setw(20) << right << i / eIndex;
        }outs << setw(20) << right << field_value;
    }
    return outs;
}

// Helper function to select records based on a given condition
vector<long> Table::selectHelp(const string& field_name, const string& op, const string& field_value) {
    if (!cache.contains(field_name)) {
        throw std::invalid_argument("Unknown field: " + field_name);
    }
        // If the operation is "=", return the record indices for the matching field_value
    else if (op == "=" && cache[field_name].contains(field_value)) {
        return cache[field_name][field_value];
    }
        // If the operation is "<=", return the record indices for all values less than or equal to field_value
    else if (op == "<=") {
        return get_record_indices(cache[field_name].begin(), cache[field_name].upper_bound(field_value));
    }
        // If the operation is "<", return the record indices for all values less than field_value
    else if (op == "<") {
        return get_record_indices(cache[field_name].begin(), cache[field_name].lower_bound(field_value));
    }
        // If the operation is ">=", return the record indices for all values greater than or equal to field_value
    else if (op == ">=") {
        return get_record_indices(cache[field_name].lower_bound(field_value), cache[field_name].end());
    }
        // If the operation is ">", return the record indices for all values greater than field_value
    else if (op == ">") {
        return get_record_indices(cache[field_name].upper_bound(field_value), cache[field_name].end());
    }
        // If the operation is "!=", return the record indices for all values not equal to field_value
    else if (op == "!=") {
        vector<long> res;
        typename MMap<string, long>::Iterator it;
        for (it = cache[field_name].begin(); it != cache[field_name].end(); ++it) {
            if ((*it).key == field_value) continue;
            res += (*it).value_list;
        }
        return res;
    }
        // If the operation is invalid, return an empty vector
    else {
        return {};
    }
}


Table Table::select(const vector<string>& selected_fields, const string& field_name, const string& op, const string& field_value){
    Table temp;
    validate_projection(selected_fields);

    // select all fields
    if (selected_fields.empty() || selected_fields[0] == "*"){
        selectedFields = names();
        temp.fieldNames = names();
        temp.selectedFields = names();
    }else{
        selectedFields = selected_fields;
        temp.selectedFields = selected_fields;
        temp.fieldNames = names();
    }

    // reorder the selected fields
    vector<string> reordered;
    vector<string> selected_temp = selectedFields;
    permutate_fields(selected_temp, reordered);
    // copy members
    temp.tableName = tableName;
    // assign the indices
    temp.recordIndices = selectHelp(field_name, op, field_value);
    recordIndices = temp.recordIndices;
    temp.numRecords = numRecords;

    // read the entries
    fstream f;
    string table_file = temp.tableName + ".bin";
    open_fileRW(f, table_file.c_str());
    for (auto idx : temp.recordIndices){
        FileRecord r;
        r.resize(fieldNames.size());
        long byte = r.read(f, idx);

        if (byte == 0) break;
        vector<string> entry = r.get_records_string();
        for (std::size_t i = 0; i < entry.size(); ++i)
            if (contains(temp.selectedFields, fieldNames[i]))
            {
                temp.printQueue += entry[i];
            }
    }
    f.close();

    return temp;
}

// accumulate version of select
Table Table::select(const vector<string>& selected_fields, const vector<string>& expression){
    Queue<Token*> infix;
    allocate_tokens_memory(expression, infix);

    ShuntingYard sy(infix);
    Queue<Token*> postfix = sy.postfix();
    const auto release_tokens = [&infix]() {
        if (infix.empty()) return;
        typename Queue<Token*>::Iterator it;
        for (it = infix.begin(); it != infix.end(); ++it) delete *it;
        infix.clear();
    };
    if (sy.is_error()) {
        release_tokens();
        throw std::invalid_argument("Invalid parenthesis structure in WHERE clause");
    }
    try {
        Table temp = select(selected_fields, postfix);
        release_tokens();
        return temp;
    } catch (...) {
        release_tokens();
        throw;
    }
}

Table Table::select(const vector<string>& selected_fields, const Queue<Token*>& expression){
    Table temp;
    validate_projection(selected_fields);

    // select all fields
    if (selected_fields.empty() || selected_fields[0] == "*"){
        selectedFields = names();
        temp.fieldNames = names();
        temp.selectedFields = names();
    }else{
        selectedFields = selected_fields;
        temp.selectedFields = selected_fields;
        temp.fieldNames = names();
    }

    // reorder the selected fields
    vector<string> reordered;
    vector<string> selected_temp = selectedFields;
    permutate_fields(selected_temp, reordered);
    // copy members
    temp.tableName = tableName;
    temp.fieldNames = reordered;
    temp.numRecords = numRecords;

    // assign the indices
    if (!expression.empty()){temp.recordIndices = Eval(expression);}
    else{
        recordIndices.clear();
        for (long i = 0; i < numRecords; ++i){
            temp.recordIndices.push_back(i);
        }
    }

    fstream f;
    string table_file = temp.tableName + ".bin";
    open_fileRW(f, table_file.c_str());
    for (auto idx : temp.recordIndices){
        FileRecord r;
        r.resize(fieldNames.size());
        long byte = r.read(f, idx);

        if (byte == 0) break;
        vector<string> entry = r.get_records_string();
        for (std::size_t i = 0; i < entry.size(); ++i)
            if (contains(temp.fieldNames, fieldNames[i])) temp.printQueue += entry[i];
    }
    f.close();
    recordIndices = temp.recordIndices;
    return temp;
}

void Table::validate_projection(const vector<string>& fields) const {
    if (fields.empty() || (fields.size() == 1 && fields[0] == "*")) return;
    for (const auto& field : fields) {
        if (!fieldNameMap.contains(field)) {
            throw std::invalid_argument("Unknown selected field: " + field);
        }
    }
}

// Function to evaluate the postfix expression and return the record indices that meet the conditions
vector<long> Table::Eval(const Queue<Token*>& postfix) {
    Stack<Token*> s;
    Queue<Token*> q = postfix;
    Stack<vector<long>> indices;

    // Iterate through the postfix expression
    while (!q.empty()) {
        Token* token = q.pop();

        // If the token is a string, push it onto the stack
        if (token->token_type() == TOKEN_TOKENSTR) {
            s.push(token);
            continue;
        }
            // If the token is a relational operator, select records from the cache
        else if (token->token_type() == TOKEN_RELATIONAL) {
            if (s.size() < 2) {
                throw std::invalid_argument("Malformed relational expression");
            }
            string relational_op = token->token_string();
            string field_value = s.pop()->token_string(); // Pop the field_value first
            string field_name = s.pop()->token_string();  // Pop the field_name second

            // Get the record indices that satisfy the condition
            vector<long> selected_indices = selectHelp(field_name, relational_op, field_value);
            indices.push(selected_indices);
        }
            // If the token is a logical operator, perform set operations on the record indices
        else if (token->token_type() == TOKEN_LOGICAL) {
            if (indices.size() < 2) {
                throw std::invalid_argument("Malformed logical expression");
            }
            vector<long> first = indices.pop();
            vector<long> second = indices.pop();

            vector<long> temp;
            temp.clear();

            // Perform intersection if the logical operator is "and"
            if (token->token_string() == "and") {
                temp = my_intersection(first, second);
            }
            // Perform union if the logical operator is "or"
            if (token->token_string() == "or") {
                temp = my_union(first, second);
            }
            indices.push(temp);
        }
    }

    // Return the final result (the record indices that meet the conditions)
    if (!s.empty() || indices.size() != 1) {
        throw std::invalid_argument("Malformed WHERE expression");
    }
    return indices.top();
}
