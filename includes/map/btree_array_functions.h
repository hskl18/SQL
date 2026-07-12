#ifndef BTREE_ARRAY_FUNCTIONS_H
#define BTREE_ARRAY_FUNCTIONS_H

#include <iostream>   // Provides std::cout
#include <iomanip>    // Provides std::setw
#include <cassert>    // Provides assert
#include <cstdlib>    // Provides size_t

// Declaration

template <class T>
void swap(T& a, T& b);                                  //swap the two items

template <class T>
T maximal(const T& a, const T& b);                      //return the larger of the two items

template <class T>
std::size_t index_of_maximal(T data[], std::size_t n);  //return index of the largest item in data with length n

template <class T>
std::size_t first_ge(const T data[], std::size_t n, const T& entry); //return the first index such that data[i] is not less than the entry
//if there is no such index, then return n indicating all data are less than the entry.

template <class T>
void insert_item(T data[], std::size_t i, std::size_t& n, T entry);  //insert entry at index i in data

template <class T>
void ordered_insert(T data[], std::size_t& n, T entry); //insert entry into the sorted array data with length n

template <class T>
void attach_item(T data[], std::size_t& n, const T& entry);          //append entry to the right of data

template <class T>
void delete_item(T data[], std::size_t i, std::size_t& n, T& entry); //delete item at index i and place it in entry

template <class T>
void detach_item(T data[], std::size_t& n, T& entry);   //remove the last element in data and place it in entry

template <class T>
void merge(T data1[], std::size_t& n1, T data2[], std::size_t& n2);  //append data2 to the right of data1 and set n2 to 0

template <class T>
void split(T data1[], std::size_t& n1, T data2[], std::size_t& n2);  //move n/2 elements from the right of data1 to beginning of data2

template <class T>
void copy_array(T dest[], const T src[], std::size_t& dest_size, std::size_t src_size); //copy all entries from src[] to replace dest[]

template <class T>
bool is_le(const T data[], std::size_t n, const T& item);//return true if item <= all data[i], otherwise return false

template <class T>
bool is_gt(const T data[], std::size_t n, const T& item);//return true if item > all data[i], otherwise return false

template <class T>
void print_array(const T data[], std::size_t n, std::size_t pos = 0);//print array data






// Definition
template <class T>
void swap(T& a, T& b){
    auto temp = a;
    a=b;
    b=temp;
}                                  //swap the two items

template <class T>
T maximal(const T& a, const T& b){
    if (a>b)return a;
    return b;
}                      //return the larger of the two items

template <class T>
std::size_t index_of_maximal(T data[], std::size_t n){
    std::size_t ans =0;
    for (std::size_t i = 1; i < n; ++i) {
        if (data[i]>data[ans]){
            ans=i;
        }
    }
    return ans;
}  //return index of the largest item in data with length n

template <class T>
std::size_t first_ge(const T data[], std::size_t n, const T& entry){
    for (std::size_t i = 0; i < n; ++i)
        if (!(data[i] < entry))
            return i;
    return n;
} //return the first index such that data[i] is not less than the entry
//if there is no such index, then return n indicating all of data are less than the entry.

template <class T>
void insert_item(T data[], std::size_t i, std::size_t& n, T entry){
    for (std::size_t j = n; j > i; --j) {
        data[j]=data[j-1];
    }
    data[i]=entry;
    n++;

}  //insert entry at index i in data

template <class T>
void ordered_insert(T data[], std::size_t& n, T entry){
    std::size_t i= first_ge(data,n,entry);
    if (i==n){
        data[i]=entry;
        n++;
    } else
        insert_item(data,i,n,entry);
} //insert entry into the sorted array data with length n

template <class T>
void attach_item(T data[], std::size_t& n, const T& entry){
    data[n]=entry;
    n++;

}//append entry to the right of data

template <class T>
void delete_item(T data[], std::size_t i, std::size_t& n, T& entry){
    entry= data[i];
    n--;
    for (std::size_t j = i; j < n; ++j) {
        data[j]=data[j+1];
    }
} //delete item at index i and place it in entry

template <class T>
void detach_item(T data[], std::size_t& n, T& entry){
    n--;
    entry=data[n];

}   //remove the last element in data and place it in entry

template <class T>
void merge(T data1[], std::size_t& n1, T data2[], std::size_t& n2){
    int g=0;
    for (std::size_t i = n1; i < n1+n2; ++i) {
        data1[i]=data2[g];
        g++;n1++;n2--;
    }
}  //append data2 to the right of data1 and set n2 to 0

template <class T>
void split(T data1[], std::size_t& n1, T data2[], std::size_t& n2){
    n2=n1/2;
    std::size_t i= (n1%2==1)?(n2+1):n2,g=0;

    for (; i < n1; ++i) {
        data2[g]=data1[i];
        g++;
    }
    n1-=n2;
}  //move n/2 elements from the right of data1 to beginning of data2

template <class T>
void copy_array(T dest[], const T src[], std::size_t& dest_size, std::size_t src_size){
    for (std::size_t i = 0; i < src_size; ++i) {
        dest[i]=src[i];
    }
    dest_size=src_size;
} //copy all entries from src[] to replace dest[]

template <class T>
bool is_le(const T data[], std::size_t n, const T& item){
    for (std::size_t i = 0; i < n; ++i) {
        if (data[i]<item)return false;
    }return true;
}//return true if item <= all data[i], otherwise return false

template <class T>
bool is_gt(const T data[], std::size_t n, const T& item){
    for (std::size_t i = 0; i < n; i++)
        if (item <= data[i]) return false;

    return true;
}//return true if item > all data[i], otherwise return false

template <class T>
void print_array(const T data[], std::size_t n, std::size_t pos){
    for (; pos < n; ++pos) {
        std::cout<<"["<<data[pos]<<"]";
    }
}//print array data


#endif // BTREE_ARRAY_FUNCTIONS_H
