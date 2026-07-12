#ifndef MYSTACK_H
#define MYSTACK_H

#include <iomanip>
#include <iostream>

#include "../linked_list_functions/linked_list_functions.h"

using namespace std;

template <typename T>
class Stack{
public:
    class Iterator{
    public:
        // Give access to list to access _ptr
        friend class Stack;

        // Default CTOR
        Iterator(){this->_ptr = nullptr;}
        // Point Iterator to where p is pointing to
        Iterator(node<T>* p){this->_ptr = p;}

        // Casting operator: true if _ptr not NULL
        // This turned out to be a pain!
        operator bool(){return !(this->_ptr == nullptr);}

        // dereference operator
        T& operator*(){
            assert(this->_ptr != nullptr);
            return this->_ptr->_item;
        }
        // member access operator
        T* operator->(){return this->_ptr;}
        // true if _ptr is NULL
        bool is_null(){return (this->_ptr == nullptr) ? true : false;}

        // true if left != right
        friend bool operator!=(const Iterator& left, const Iterator& right){
            return left._ptr != right._ptr;
        }
        // true if left == right
        friend bool operator==(const Iterator& left, const Iterator& right){
            return left._ptr == right._ptr;
        }

        // member operator:  ++it; or ++it = new_value
        Iterator& operator++(){
            this->_ptr = this->_ptr->_next;
            return *this;
        }
        // friend operator: it++
        friend Iterator operator++(Iterator& it, int){
            Iterator hold = it;
            it._ptr = it._ptr->_next;
            return hold;
        }

    private:
        node<T>* _ptr; // pointer being encapsulated
    };

    // constructor: CTOR
    Stack();

    // BIG 3:
    Stack(const Stack<T>& copyMe);
    ~Stack();
    Stack<T>& operator=(const Stack<T>& RHS);

    // Operations:
    void push(T item);
    T pop();

    // Accessors:
    Iterator begin() const; // Iterator to the head node
    Iterator end() const;   // Iterator to NULL

    // Checkers:
    int size() const;
    bool empty();
    bool empty() const{ return this->_top==nullptr;}
    T top();

    template <typename U>
    friend ostream& operator<<(ostream& outs, const Stack<U>& printMe);
    void clear(){
        _clear_list<T>(this->_top);
        this->_size = 0;
    }

private:
    node<T>* _top;
    int _size;
};

// Definition

// TODO

template <typename T>
Stack<T>::Stack(){
    this->_size = 0;
    this->_top = nullptr;
}

template <typename T>
Stack<T>::Stack(const Stack<T>& copyMe){
    this->_top = _copy_list<T>(copyMe._top);
    this->_size = copyMe.size();
}

template <typename T>
Stack<T>::~Stack(){
    _clear_list<T>(this->_top);
    this->_size = 0;
}
template <typename T>
Stack<T>& Stack<T>::operator=(const Stack<T>& RHS){
    if (this == &RHS) return *this;
    _clear_list<T>(this->_top);
    this->_top = _copy_list<T>(RHS._top);
    this->_size = RHS._size;
    return *this;
}

template <typename T>
void Stack<T>::push(T item){
    this->_top = _insert_head<T>(this->_top, item);
    this->_size++;
}

template <typename T>
T Stack<T>::pop(){
    if (this->_size > 0) this->_size--;
    return _delete_node<T>(this->_top, this->_top);
}

template <typename T>
typename Stack<T>::Iterator Stack<T>::begin() const{
    Iterator it(this->_top);
    return it;
}

template <typename T>
typename Stack<T>::Iterator Stack<T>::end() const{
    Iterator it(_last_node(this->_top)->_next);
    return it;
}

template <typename T>
int Stack<T>::size() const{
    return this->_size;
}

template <typename T>
bool Stack<T>::empty(){
    return (this->_top == nullptr);
}

template <typename T>
T Stack<T>::top(){
    if (this->empty()) return T();
    return this->_top->_item;
}

template <typename U>
ostream& operator<<(ostream& outs, const Stack<U>& printMe){
    typename Stack<U>::Iterator it;
    if (printMe.empty())
    {
        outs << "|||";
        return outs;
    }
    for (it = printMe.begin(); it != printMe.end(); it++)
    {
        if (it) outs << "[" << *it << "]->";
    }
    outs << "|||" << endl;
    return outs;
}

#endif // MYSTACK_H
