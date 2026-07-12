#ifndef MYQUEUE_H
#define MYQUEUE_H

#include "../linked_list_functions/linked_list_functions.h"
#include <iomanip>
#include <iostream>
#include <vector>

using namespace std;

template <typename T>
class Queue{
public:
    class Iterator{
    public:
        // Give access to list to access _ptr
        friend class Queue;

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
        bool is_null(){return (this->_ptr == nullptr);}

        // true if left != right
        friend bool operator!=(const Iterator& left, const Iterator& right){return left._ptr != right._ptr;}
        // true if left == right
        friend bool operator==(const Iterator& left, const Iterator& right){return left._ptr == right._ptr;}

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
    Queue();

    // BIG 3:
    Queue(const Queue<T>& copyMe);
    ~Queue();
    Queue<T>& operator=(const Queue<T>& RHS);

    T& operator[](int i){
        typename Queue<T>::Iterator it;
        int idx = 0;
        for (it = begin(); it != end(); it++){
            if (idx == i) break;
            idx++;
        }
        return *it;
    }
    const T& operator[](int i) const{return (*this)[i];}
    Queue<T>& operator+=(const vector<T>& items){
        for (std::size_t i = 0; i < items.size(); i++){
            this->push(items[i]);
        }
        return *this;
    }
    Queue<T>& operator+=(const T& item){
        this->push(item);
        return *this;
    }
    // Operations:
    void push(T item); // Enqueue
    T pop();           // Dequeue

    // Accessors:
    Iterator begin() const; // Iterator to the head node
    Iterator end() const;   // Iterator to NULL

    // Checkers:
    int size() const;
    bool empty();
    bool empty() const{
        if (this->_rear == nullptr || this->_front == nullptr) return true;
        return false;
    }
    T front();
    T back();

    template <typename U>
    friend ostream& operator<<(ostream& outs, const Queue<U>& printMe);
    void clear(){
        _clear_list<T>(this->_front);
        this->_front = nullptr;
        this->_rear = nullptr;
        this->_size = 0;
    }

private:
    node<T>* _front;
    node<T>* _rear;
    int _size;
};

// Definition

// TODO

template <typename T>
Queue<T>::Queue(){
    this->_front = nullptr;
    this->_rear = nullptr;
    this->_size = 0;
}

template <typename T>
Queue<T>::Queue(const Queue<T>& copyMe){
    this->_front = nullptr;
    this->_rear = nullptr;
    this->_rear = _copy_list<T>(this->_front, copyMe._front);
    this->_size = copyMe.size();
}

template <typename T>
Queue<T>::~Queue(){
    _clear_list<T>(this->_front);
    this->_front = nullptr;
    this->_rear = nullptr;
    this->_size = 0;
}

template <typename T>
Queue<T>& Queue<T>::operator=(const Queue<T>& RHS){
    if (this == &RHS) return *this;
    _clear_list<T>(this->_front);
    this->_front = nullptr;
    this->_rear = nullptr;
    this->_rear = _copy_list<T>(this->_front, RHS._front);
    this->_size = RHS._size;
    return *this;
}

template <typename T>
void Queue<T>::push(T item){
    this->_size++;
    if (this->empty()){
        this->_front = _insert_head(this->_front, item);
        this->_rear = this->_front;
        return;
    }
    this->_rear = _insert_after<T>(this->_front, _last_node<T>(this->_front), item);
}

template <typename T>
T Queue<T>::pop(){
    if (this->_size > 0) this->_size--;
    return _delete_node<T>(this->_front, this->_front);
}

template <typename T>
typename Queue<T>::Iterator Queue<T>::begin() const{
    Iterator it(this->_front);
    return it;
}

template <typename T>
typename Queue<T>::Iterator Queue<T>::end() const{
    Iterator it(_last_node(this->_front)->_next);
    return it;
}

template <typename T>
int Queue<T>::size() const{
    return this->_size;
}

template <typename T>
bool Queue<T>::empty(){
    if (this->_front == nullptr || this->_rear == nullptr) return true;
    return false;
}

template <typename T>
T Queue<T>::front(){
    Iterator it(this->_front);
    return *it;
}

template <typename T>
T Queue<T>::back(){
    Iterator it(this->_rear);
    return *it;
}

template <typename U>
ostream& operator<<(ostream& outs, const Queue<U>& printMe){
    typename Queue<U>::Iterator it;
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

#endif // MYQUEUE_H
