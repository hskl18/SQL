#ifndef BPLUSTREE_H
#define BPLUSTREE_H

#include <iostream>   // Provides std::cout
#include <iomanip>    // Provides std::setw
#include <cassert>    // Provides assert
#include <cstdlib>    // Provides size_t
#include <string>     // Provides std::to_string

#include "btree_array_functions.h" // Include the implementation.

template <class Item>
class BPlusTree{
public:

    // TYPEDEFS and MEMBER CONSTANTS
    typedef Item value_type;
    // NESTED CLASS DEFINITION
    class Iterator{
    public:
        friend class BPlusTree;
        // CONSTRUCTORS
        explicit Iterator(BPlusTree* _it=nullptr, std::size_t _key_ptr = 0):
                it(_it), key_ptr(_key_ptr) {}
        // MODIFICATION MEMBER FUNCTIONS

        Item operator * (){
            assert(this->it!= nullptr);
            return this->it->data[this->key_ptr];
        }
        const Item operator *() const{
            assert(this->it!= nullptr);
            return this->it->data(this->key_ptr);
        }

        // Postfix ++ (it++)
        Iterator operator ++ (int){
            Iterator ans = *this;
            if(this->key_ptr == this->it->data_count-1){
                this->it=this->it->next;
                this->key_ptr=0;
                return ans;
            }
            this->key_ptr++;
            return ans;
        }
        // Prefix ++ (++it)
        Iterator operator ++ (){
            if (this->key_ptr==this->it->data_count -1)
            {
                this->key_ptr=0;
                this->it=this->it->next;
                return *this;
            }
            this->key_ptr++;
            return *this;
        }

        // OVERLOADED OPERATORS
        friend bool operator == (const Iterator& lhs, const Iterator& rhs){
            return (lhs.it==rhs.it && lhs.key_ptr==rhs.key_ptr);
        }
        friend bool operator != (const Iterator& lhs, const Iterator& rhs){
            return !(lhs == rhs);
        }
        // HELPER FUNCTIONS
        bool is_null () { return !it; }
        void print_Iterator(){
            if (!this->is_null()){
                print_array(this->it->data,this->it->data_count);
            }else std::cout<<"nullptr iterator"<<std::endl;
        }
        void info() const;

    private:
        BPlusTree<Item>* it;
        std::size_t key_ptr;
    };


    // CONSTRUCTORS and DESTRUCTOR
    BPlusTree();
    BPlusTree(const BPlusTree& source);
    BPlusTree(Item* a, std::size_t size);
    ~BPlusTree() { clear_tree(); }
    // MODIFICATION MEMBER FUNCTIONS
    BPlusTree& operator = (const BPlusTree& source);
    void clear_tree();
    void copy_tree(const BPlusTree& source);
    void copy_tree(const BPlusTree& source, BPlusTree*& last_node);
    bool insert(const Item& entry);
    bool erase(const Item& target);

    // NON-CONSTANT MEMBER FUNCTIONS
    Iterator find(const Item& entry);
    Item& get(const Item& entry);

    Iterator lower_bound(const Item& key);
    Iterator upper_bound(const Item& key);
    Iterator begin();
    Iterator end() { return Iterator(); }

    // CONSTANT MEMBER FUNCTIONS
    bool contains(const Item& entry) const;
    const Item& get(const Item& entry) const;

    std::size_t size();
    std::size_t size_list();
    std::size_t count() const;

    bool empty() const { return (data_count == 0 && is_leaf()); }
    void print(int indent=0, std::ostream& outs = std::cout) const;
    bool is_valid() const;
    // OVERLOAD OPERATOR FUNCTIONS
    friend std::ostream& operator << (std::ostream& outs, const BPlusTree<Item>& btree) {
        btree.print(0, outs);
        return outs;
    }
    // SUGGESTED FUNCTION FOR DEBUGGING
    std::string in_order(){return "";}
    std::string pre_order(){return "";}
    std::string post_order(){return "";}
private:
    // MEMBER CONSTANTS
    static const std::size_t MINIMUM = 1;
    static const std::size_t MAXIMUM = 2 * MINIMUM;
    // MEMBER VARIABLES
    std::size_t data_count;
    Item data[MAXIMUM+1];
    std::size_t child_count;
    BPlusTree* subset[MAXIMUM+2];
    BPlusTree* next;

    // HELPER MEMBER FUNCTIONS
    bool is_leaf() const { return (child_count == 0); }
    BPlusTree<Item>* get_smallest_node();

    // insert element functions
    bool loose_insert(const Item& entry);
    void fix_excess(std::size_t i);

    // remove element functions
    void get_small(Item& good);
    bool loose_erase(const Item& target);
    BPlusTree<Item>* fix_shortage(std::size_t i);
//    void delete_internal_node(std::size_t i, const Item& target, const Item& next_smallest);
    void transfer_from_left(std::size_t i);
    void transfer_from_right(std::size_t i);
    BPlusTree<Item>* merge_with_next_subset(std::size_t i);

    Item* get_Item(const Item& entry){
        size_t i = first_ge(this->data, this->data_count, entry);
        bool found = (i < this->data_count) && !(entry < this->data[i]);

        if (this->is_leaf() && found){
            return &this->data[i];
        }else if(!this->is_leaf() && found){
            return this->subset[i+1]->get_Item(entry);
        }else if(!this->is_leaf() && !found){
            return this->subset[i]->get_Item(entry);
        }
        else return nullptr;
    }
};



template<class Item>
BPlusTree<Item>::BPlusTree():data_count(0),child_count(0),next(nullptr) {}

template<class Item>
BPlusTree<Item>::BPlusTree(const BPlusTree &source) : BPlusTree() {
    this->copy_tree(source);
}

template<class Item>
BPlusTree<Item>::BPlusTree(Item *a, std::size_t size) : BPlusTree() {
    for (size_t i = 0; i < size; ++i) this->insert(a[i]);
}

template<class Item>
BPlusTree<Item> &BPlusTree<Item>::operator=(const BPlusTree &source) {
    if(this == &source)return *this;
    this->clear_tree();
    this->copy_tree(source);
    return *this;
}

template<class Item>
void BPlusTree<Item>::clear_tree() {
    for (size_t i = 0; i < this->child_count; ++i) {
        this->subset[i]->clear_tree();
        delete this->subset[i];
    }
    this->data_count=0;
    this->child_count=0;
    this->next= nullptr;
}

template<class Item>
void BPlusTree<Item>::copy_tree(const BPlusTree &source) {
    BPlusTree<Item>* last = nullptr;
    this->copy_tree(source,last);
}

template<class Item>
void BPlusTree<Item>::copy_tree(const BPlusTree &source, BPlusTree *&last_node) {
    copy_array(this->data,source.data,this->data_count,source.data_count);
    this->next= nullptr;

    this->child_count = source.child_count;
    for (std::size_t i = 0; i < this->child_count; ++i) {
        this->subset[i]=new BPlusTree<Item>();
        this->subset[i]->copy_tree(*source.subset[i], last_node);
    }
    if (source.is_leaf()){
        if (last_node!= nullptr){
            last_node->next=this;
        }
        last_node=this;
    }
}

template <class Item>
typename BPlusTree<Item>::Iterator BPlusTree<Item>::find(const Item& entry)
{
    size_t index = first_ge(this->data, this->data_count, entry);
    bool found = (index < this->data_count) && !(entry < this->data[index]);
    // leaf and found
    if (this->is_leaf() && found)
        return Iterator(this, index);
    // interal node and found
    if (!this->is_leaf() && found)
        return this->subset[index + 1]->find(entry);
    // internal node and not found
    if (!this->is_leaf() && !found)
        return this->subset[index]->find(entry);
    // leaf and not found
    return Iterator();
}

template <class Item>
bool BPlusTree<Item>::contains(const Item& entry) const
{
    size_t index = first_ge(this->data, this->data_count, entry);
    bool found = (index < this->data_count) && !(entry < this->data[index]);

    // leaf and found
    if (this->is_leaf() && found)
        return true;
    // interal node and found
    if (!this->is_leaf() && found)
        return this->subset[index + 1]->contains(entry);
    // internal node and not found
    if (!this->is_leaf() && !found)
        return this->subset[index]->contains(entry);

    // if leaf and not found
    return false;
}

template<class Item>
const Item &BPlusTree<Item>::get(const Item &entry) const {
    if(!this->contains(entry))this->insert(entry);
    return *this->get_Item(entry);
}

template<class Item>
Item &BPlusTree<Item>::get(const Item &entry) {
    if(!this->contains(entry))this->insert(entry);
    return *this->get_Item(entry);
}

template<class Item>
std::size_t BPlusTree<Item>::size() {
    size_t i=0;
    if(this->empty()) return i;
//    typename BPlusTree<Item>::Iterator end = this->begin();
//    auto end = this->begin();
    for(auto begin = this->begin(); begin !=this->end(); ++begin){
        i++;
    }
    return i;
}

template<class Item>
std::size_t BPlusTree<Item>::size_list() {
    return 0;
}

template <class Item>
typename BPlusTree<Item>::Iterator BPlusTree<Item>::lower_bound(const Item& key){
    Iterator it = this->begin();
    for (it = this->begin(); it != this->end(); ++it)
    {
        assert(!it.is_null());
        if (!(*it < key)) return it;
    }
    return this->end();
}

template<class Item>
typename BPlusTree<Item>::Iterator BPlusTree<Item>::upper_bound(const Item &key) {
    for (Iterator ans =this->begin(); ans != this->end(); ++ans) {
        assert(!ans.is_null());
        if (*ans > key)return ans;
    }
    return this->end();
}

template<class Item>
typename BPlusTree<Item>::Iterator BPlusTree<Item>::begin() {
    return BPlusTree::Iterator(this->get_smallest_node());
}


template<class Item>
std::size_t BPlusTree<Item>::count() const {
    size_t ans = this->data_count;
    for (size_t i = 0; i < child_count; ++i) {
        ans += this->subset[i]->count();
    }
    return ans;
}

template<class Item>
void BPlusTree<Item>::print(int indent, std::ostream &outs) const {
    static const std::string down_bracket = "\357\271\207"; // ﹇
    static const std::string up_bracket = "\357\271\210";   // ﹈

    if(this->child_count>this->data_count)this->subset[this->child_count-1]->print(indent+1,outs);
    outs << std::setw(indent * 4) <<"" <<down_bracket << std::endl;

    if (this->data_count!=0){
        for (size_t i = this->data_count-1;  i<this->data_count ; --i) {
            outs << std::setw(indent * 4) <<""<< this->data[i] << std::endl;
            if(i==0)outs << std::setw(indent * 4)<<"" << up_bracket << std::endl;
            if(i<this->child_count)this->subset[i]->print(indent+1,outs);
        }
    }else outs << std::setw(indent * 4) << ""<<up_bracket << std::endl;
}


template<class Item>
BPlusTree<Item> *BPlusTree<Item>::get_smallest_node() {
    if(this->is_leaf())return this;
    return this->subset[0]->get_smallest_node();
}

template<class Item>
void BPlusTree<Item>::get_small(Item &good) {
    if (this->is_leaf()) {
        good = this->data[0];
        return;
    }
    this->subset[0]->get_small(good);
}

template<class Item>
bool BPlusTree<Item>::insert(const Item &entry) {
    if (!this->loose_insert(entry))return false;
    if (this->data_count > BPlusTree<Item>::MAXIMUM){

        auto ans = new BPlusTree<Item>();
        copy_array(ans->data,this->data,ans->data_count,this->data_count);
        copy_array(ans->subset,this->subset,ans->child_count,this->child_count);

        this->data_count=0;
        this->child_count=1;
        this->subset[0]=ans;

        this->fix_excess(0);
    }
    return true;}

template<class Item>
bool BPlusTree<Item>::loose_insert(const Item &entry) {
    size_t ge = first_ge(this->data,this->data_count,entry);
    bool ans =false;
    bool found = (ge < this->data_count) && !(entry < this->data[ge]);

    if (this->is_leaf() && found)return false;
    else if (this->is_leaf() && !found){
        insert_item(this->data,ge,this->data_count,entry);
        return true;
    } else if (!this->is_leaf() && found){
        ans = this->subset[ge+1]->loose_insert(entry);
        if (this->subset[ge+1]->data_count >MAXIMUM)this->fix_excess(ge+1);
    } else {
        ans = this->subset[ge]->loose_insert(entry);
        if (this->subset[ge]->data_count >MAXIMUM)this->fix_excess(ge);
    }
    return ans;
}

template<class Item>
void BPlusTree<Item>::fix_excess(std::size_t i) {
    auto sb = this->subset[i];
    auto sp = new BPlusTree<Item>();
    split(sb->data,sb->data_count,sp->data,sp->data_count);
    split(sb->subset,sb->child_count,sp->subset,sp->child_count);

    Item newThing;
    detach_item(sb->data,sb->data_count,newThing);
    ordered_insert(this->data,this->data_count,newThing);
    if (sb->is_leaf()){
        ordered_insert(sp->data,sp->data_count,newThing);
        sp->next=sb->next;
        sb->next=sp;
    }
    insert_item(this->subset,i+1,this->child_count,sp);
}

template<class Item>
bool BPlusTree<Item>::erase(const Item &target) {
    if (!this->loose_erase(target)) return false;

    if (this->data_count<MINIMUM&& this->child_count> 0){
        BPlusTree<Item>* child = this->subset[0];
        copy_array(this->data, child->data, this->data_count, child->data_count);
        copy_array(this->subset, child->subset, this->child_count, child->child_count);
        child->child_count = 0;
        delete child;
    }
    return true;
}

template <class Item>
bool BPlusTree<Item>::loose_erase(const Item& entry){
    size_t ge = first_ge(this->data, this->data_count, entry);
    bool found = (ge < this->data_count) && !(entry < this->data[ge]);
    bool ans = false;

    if (this->is_leaf() && !found)return false;
    else if (this->is_leaf() && found){
        Item item;
        delete_item(this->data, ge, this->data_count, item);
        return true;

    }else if (!this->is_leaf() && !found){
        ans = this->subset[ge]->loose_erase(entry);
        if (this->subset[ge]->data_count < MINIMUM) this->fix_shortage(ge);

    }else{
        // internal and found
        ans = subset[ge + 1]->loose_erase(entry);

        if (this->subset[ge + 1]->data_count < MINIMUM){
            // fix shortage returns the subset where it's fixed
            BPlusTree<Item>* fixed_subset = fix_shortage(ge + 1);
            size_t data_idx = first_ge(this->data, this->data_count, entry);
            size_t subset_idx = first_ge(fixed_subset->data, fixed_subset->data_count, entry);

            // found in data
            bool foundx = data_idx < this->data_count && !(entry < this->data[data_idx]);
            if (data_idx < this->child_count - 1 && foundx){
                // replace data with smallest
                this->subset[data_idx + 1]->get_small(this->data[data_idx]);
                return ans;
            }
            // found in fixed subset
            foundx = subset_idx < fixed_subset->data_count && !(entry < fixed_subset->data[subset_idx]);
            if (subset_idx < fixed_subset->child_count - 1 && foundx){
                // replace data with smallest
                fixed_subset->subset[subset_idx + 1]->get_small(fixed_subset->data[subset_idx]);
                return ans;
            }
        }
        // no need to fix, just replace
        if (ge < this->data_count && this->subset[ge + 1]->data_count >= MINIMUM){
            this->subset[ge + 1]->get_small(this->data[ge]);
            return ans;
        }
    }
    return ans;
}

template <class Item>
BPlusTree<Item>* BPlusTree<Item>::fix_shortage(std::size_t i){
    // transfer from left to right
    if (i > 0 && this->subset[i - 1]->data_count > MINIMUM){
        this->transfer_from_left(i);
        return subset[i];
    }
    // transfer from right to left
    if (i < this->child_count - 1 && this->subset[i + 1]->data_count > MINIMUM){
        this->transfer_from_right(i);
        return subset[i];
    }
    // merge subset[i - 1] with subset[i]
    if (i > 0 && this->subset[i - 1]->data_count <= MINIMUM) return this->merge_with_next_subset(i - 1);
    // merge subset[i] with subset[i + 1]
    if (i < this->child_count - 1 && this->subset[i + 1]->data_count <= MINIMUM) return this->merge_with_next_subset(i);
    // do nothing
    return subset[i];
}


template <class Item>
void BPlusTree<Item>::transfer_from_left(std::size_t i){
    BPlusTree<Item>* left_subset = this->subset[i - 1];
    BPlusTree<Item>* right_subset = this->subset[i];
    Item item;
    // left not leaf
    if (!left_subset->is_leaf()){
        delete_item(this->data, i - 1, this->data_count, item);
        ordered_insert(right_subset->data, right_subset->data_count, item);

        detach_item(left_subset->data, left_subset->data_count, item);
        ordered_insert(this->data, this->data_count, item);

        BPlusTree<Item>* node;
        detach_item(left_subset->subset, left_subset->child_count, node);
        insert_item(right_subset->subset, 0, right_subset->child_count, node);

        return;
    }
    detach_item(this->subset[i - 1]->data, this->subset[i - 1]->data_count, this->data[i - 1]);
    insert_item(this->subset[i]->data, 0, this->subset[i]->data_count, data[i - 1]);
}

template <class Item>
void BPlusTree<Item>::transfer_from_right(std::size_t i){
    BPlusTree<Item>* left_subset = this->subset[i];
    BPlusTree<Item>* right_subset = this->subset[i + 1];
    Item item;

    if (!right_subset->is_leaf()){
        delete_item(this->data, i, this->data_count, item);
        ordered_insert(left_subset->data, left_subset->data_count, item);
        // delete the first entry in right subset data
        delete_item(right_subset->data, 0, right_subset->data_count, item);
        ordered_insert(this->data, this->data_count, item);

        BPlusTree<Item>* node;
        delete_item(right_subset->subset, 0, right_subset->child_count, node);
        attach_item(left_subset->subset, left_subset->child_count, node);
        return;
    }
    Item entry;
    delete_item(this->subset[i + 1]->data, 0, this->subset[i + 1]->data_count, entry);
    attach_item(this->subset[i]->data, this->subset[i]->data_count, entry);
    this->data[i] = this->subset[i + 1]->data[0];
}

template <class Item>
BPlusTree<Item>* BPlusTree<Item>::merge_with_next_subset(std::size_t i){
    BPlusTree<Item>* left_subset = this->subset[i];
    Item item;
    delete_item(this->data, i, this->data_count, item);
    if (!left_subset->is_leaf()) attach_item(left_subset->data, left_subset->data_count, item);
    // merge
    merge(this->subset[i]->data, this->subset[i]->data_count, this->subset[i + 1]->data, this->subset[i + 1]->data_count);
    merge(this->subset[i]->subset, this->subset[i]->child_count, this->subset[i + 1]->subset, this->subset[i + 1]->child_count);

    BPlusTree<Item>* node;
    delete_item(this->subset, i + 1, this->child_count, node);
    // relink if left is leaf
    if (left_subset->is_leaf())
        left_subset->next = node->next;
    node->child_count = 0;
    delete node;
    return subset[i];
}


template <class Item>
bool BPlusTree<Item>::is_valid() const
{
    bool valid = true;
    // check if the node is empty
    if (empty()) return true;
    // check if the node has too many entries
    if (data_count > MAXIMUM || data_count < MINIMUM) {
        return false;
    }
    // check if the node has too many children
    if (child_count > MAXIMUM+1 || child_count < 0) {
        return false;
    }
    // check if the data is sorted
    for (size_t i=0; i<data_count-1; i++) {
        if (data[i] > data[i+1]) return false;
    }
    if (!is_leaf()) {
        // check if the child_count is not equal to data_count+1
        if (child_count != data_count+1) {
            return false;
        }
        // check if data is in range of children
        for (size_t i=0; i<data_count; i++) {
            // check if data[i] is greater than subset[i]
            valid = is_gt(subset[i]->data, subset[i]->data_count, data[i]);
            if (!valid) return false;
            // check if data[i] is less than subset[i+1]
            valid = is_le(subset[i+1]->data, subset[i+1]->data_count, data[i]);
            if (!valid) return false;
            // check if subset[i] is valid
            valid = subset[i]->is_valid();
            if (!valid) return false;
            // check if data[i] is the smallest node in subset[i+1]
            valid = data[i] == subset[i+1]->get_smallest_node()->data[0];
            if (!valid) return false;
        }
        // check if the last child is valid
        valid = subset[data_count]->is_valid();
        if (!valid) return false;
    }
    return true;
}

// TODO

#endif // BPLUSTREE_H
