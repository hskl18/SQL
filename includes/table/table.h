#ifndef TABLE_H
#define TABLE_H

#include <algorithm>
#include <cassert> // assert
#include <cstdlib>
#include <cstring>  // strcmp
#include <fstream>  // fstream
#include <iomanip>  // setw
#include <iostream> // cout, endl
#include <iostream>
#include <set>    // set
#include <string> // string
#include <string>
#include <utility>
#include <vector> // vector
#include <vector>

#include "../binary_files/file_record.h"
#include "../binary_files/utilities.h"
#include "../linked_list/queue/MyQueue.h"
#include "../map/map.h"
#include "../map/mmap.h"
#include "../shunting_yard/shunting_yard.h"
#include "../shunting_yard/leftparen.h"
#include "../shunting_yard/logical.h"
#include "../shunting_yard/relational.h"
#include "../shunting_yard/rightparen.h"
#include "../shunting_yard/token.h"
#include "../shunting_yard/tokenstr.h"

using namespace std;

template <class T>
inline bool contains(const vector<T>& v, const T& target){
    for (std::size_t i = 0; i < v.size(); ++i)if (v[i] == target) return true;
    return false;
}

inline bool comparitor(const pair<string, long>& a, const pair<string, long>& b){return a.second < b.second;}

inline void my_sort(vector<string>& a, vector<long>& b){
    if (a.size() != b.size()) return;

    vector<pair<string, long>> to_sort;
    for (std::size_t i = 0; i < a.size(); ++i){to_sort.push_back(make_pair(a[i], b[i]));}
    sort(to_sort.begin(), to_sort.end(), comparitor);

    a.clear();
    b.clear();
    for (auto p : to_sort){
        a.push_back(p.first);
        b.push_back(p.second);
    }
}

inline vector<long> my_union(vector<long>& a, vector<long>& b){
    vector<long> combined = a;
    combined.insert(combined.end(), b.begin(), b.end());

    // Sort the combined vector
    sort(combined.begin(), combined.end());

    // Erase duplicate elements
    auto it = unique(combined.begin(), combined.end());
    combined.erase(it, combined.end());

    // Return the result
    return combined;
}

inline vector<long> my_intersection(vector<long>& a, vector<long>& b){
    sort(a.begin(), a.end());
    sort(b.begin(), b.end());

    // Find the intersection using set_intersection
    vector<long> intersection;
    set_intersection(a.begin(), a.end(), b.begin(), b.end(),back_inserter(intersection));

    return intersection;
}

// change every variable name used
class Table{
public:
    // TYPEDEFS
    typedef vector<string> vector_str;
    typedef vector<long> vector_long;
    typedef Map<string, long> map_sl;
    typedef Map<string, string> map_ss;
    typedef MMap<string, long> mmap_sl;
    typedef MMap<string, string> mmap_ss;

    // CONSTRUCTORS
    Table();
    Table(const string& table_name);
    Table(const string& table_name, const vector<string>& fieldNames);
    Table& operator=(const Table& RHS);
    ~Table() {}

    // SQL: CREATE TABLE
    string create_table(const string& tableName, const vector<string>& fieldNames);

    // SQL: INSERT INTO
    // string insert_into(const vector<char*>& field_values); ??
    string insert_into(const vector<string>& fieldValues);

    // SQL: SELECT
    Table select(const vector<string>& sField, const string& fieldName, const string& op, const string& fieldValue);
    Table select(const vector<string>& sField, const vector<string>& expression = vector<string>());
    Table select(const vector<string>& sField, const Queue<Token*>& expression);

    // Get all selected record numbers
    inline vector<long> select_recnos() const { return recordIndices; }
    // Print table
    friend ostream& operator<<(ostream& outs, const Table& t);
    // Get the title of the table
    inline string title() const { return tableName; }
    // Get the fields of the table
    inline vector<string> get_fields() const { return selectedFields; }
    // Get the number of records in the table
    inline long record_count() const { return select_recnos().size(); }
private:
    // iterator typedef
    typedef MMap<string, long>::Iterator mmap_iter;
    // title of the table
    string tableName;
    // fields of the table
    vector<string> fieldNames;
    vector<string> selectedFields;
    // number of records in the table
    long numRecords;
    // selected records' numbers
    vector<long> recordIndices;

    Map<string, MMap<string, long>> cache;
    Map<string, long> fieldNameMap; // map field name to entry indices

    vector<string> printQueue;

    static inline void allocate_tokens_memory(const vector<string>& a, Queue<Token*>& q){
        for (auto toke : a){
            if (toke == ">=" || toke == "<=" || toke == ">" || toke == "<" || toke == "=" || toke == "!="){
                q.push(new Relational(toke));
                continue;
            }else if (toke == "("){
                q.push(new LeftParen());
                continue;
            }else if (toke == ")"){
                q.push(new RightParen());
                continue;
            }else if (toke == "and" || toke == "or"){
                q.push(new Logical(toke));
                continue;
            }else{
                q.push(new TokenStr(toke));
            }
        }
    }

    vector<long> selectHelp(const string& field_name, const string& op, const string& field_value); // change name
    vector<long> Eval(const Queue<Token*>& eva);
//
    static vector<long> get_record_indices(const mmap_iter& begin, const mmap_iter& end){
        vector<long> res;
        res.clear();
        mmap_iter it = begin;
        if (it.is_null()) return res;
        for (it = begin; it != end; ++it) res += (*it).value_list;

        return res;
    }

    vector<string> names() { return fieldNames; }
    inline void permutate_fields(const vector<string>& sField, vector<string>& PV){
        vector<string> permutated = sField;
        // search for invalids
        vector<int> invalid_indices;
        for (std::size_t i = 0; i < permutated.size(); ++i){
            string field_name = permutated[i];
            if (!fieldNameMap.contains(field_name)) invalid_indices.push_back(i);
        }
        // erase the invalid indices
        for (auto invalid_i : invalid_indices){
            auto it = permutated.begin();
            permutated.erase(it + invalid_i);
        }

        vector<long> sort_indices;
        // sort
        for (auto field_name : permutated){
            long idx = fieldNameMap[field_name];
            sort_indices += idx;
        }

        my_sort(permutated, sort_indices);
        PV = permutated;
    }

};


#endif // TABLE_H
