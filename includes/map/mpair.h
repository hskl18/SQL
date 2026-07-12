#ifndef MPAIR_H
#define MPAIR_H

#include <cstdlib> // Provides std::size_t
#include <iostream>
#include <vector> // Provides std::vector
using namespace std;

template <typename K, typename V>
struct MPair{
    K key;
    std::vector<V> value_list;
    MPair(const K& k = K()): key(k), value_list(vector<V>()){}
    MPair(const K& k, const V& v): MPair(k){this->value_list.push_back(v);}
    MPair(const K& k, const std::vector<V>& vlist): key(k), value_list(vlist){}
    //--------------------------------------------------------------------------------

    // Overloaded operators
    friend std::ostream& operator<<(std::ostream& outs, const MPair<K, V>& print_me){
        outs << print_me.key << ":";
        for(std::size_t i = 0; i < print_me.value_list.size(); ++i) outs << print_me.value_list[i] << " ";
        outs << endl;
        return outs;
    }
    friend bool operator==(const MPair<K, V>& lhs, const MPair<K, V>& rhs){
        return lhs.key == rhs.key;
    }
    friend bool operator<(const MPair<K, V>& lhs, const MPair<K, V>& rhs){
        return lhs.key < rhs.key;
    }
    friend bool operator<=(const MPair<K, V>& lhs, const MPair<K, V>& rhs){
        return lhs.key <= rhs.key;
    }
    friend bool operator>(const MPair<K, V>& lhs, const MPair<K, V>& rhs){
        return lhs.key > rhs.key;
    }
    friend MPair<K, V> operator+(const MPair<K, V>& lhs, const MPair<K, V>& rhs){
        MPair<K, V> mpair(lhs.key, lhs.value_list);
        for (std::size_t i = 0; i < rhs.value_list.size(); ++i)
            mpair.value_list.push_back(rhs.value_list[i]);
        return mpair;
    }
    MPair<K, V>& operator+=(const V& value){
        this->value_list.push_back(value);
        return *this;
    }
};

template <typename Item>
std::ostream& operator<<(std::ostream& outs, const std::vector<Item>& vec){
    for (std::size_t i = 0; i < vec.size(); ++i) outs << vec[i] << " ";
    outs << endl;
    return outs;
}

template <typename Item>
std::vector<Item>& operator+=(std::vector<Item>& vec, const Item& value){
    vec.push_back(value);
    return vec;
}
template <typename Item>
std::vector<Item>& operator+=(std::vector<Item>& lhs, const std::vector<Item>& rhs)
{
    for (std::size_t i = 0; i < rhs.size(); ++i) lhs.push_back(rhs[i]);
    return lhs;
}

#endif // MPAIR_H
