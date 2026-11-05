#ifndef NODE_H
#define NODE_H

#include <iostream>

using namespace std;

template <typename T>
struct node{
public:
    node(const T& item = T(), node* next = nullptr);

    template <typename U>
    friend ostream& operator <<(ostream& outs, const node<U> &printMe);

    T _item;
    node* _next;
    node* _prev;
};

// Definition

template <typename T>
node<T>::node(const T& item, node<T>* next){
    this->_item = item;
    this->_next = next;
    this->_prev = nullptr;
}

template <typename U>
ostream& operator <<(ostream& outs, const node<U> &printMe){
    outs << "<-[" << printMe._item << "]->";
    return outs;
}
#endif // NODE_H 