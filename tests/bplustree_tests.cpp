#include "includes/map/bplustree.h"

#include <algorithm>
#include <iostream>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<int> values(BPlusTree<int>& tree) {
    std::vector<int> result;
    for (auto iterator = tree.begin(); iterator != tree.end(); ++iterator) {
        result.push_back(*iterator);
    }
    return result;
}

std::vector<int> values(const std::set<int>& reference) {
    return {reference.begin(), reference.end()};
}

void expect_matches(BPlusTree<int>& tree, const std::set<int>& reference) {
    expect(tree.is_valid(), "B+ tree invariants should hold");
    expect(tree.size() == reference.size(), "tree size should match std::set");
    expect(values(tree) == values(reference), "iteration should be ordered and complete");
}

void test_fixed_seed_insertions_and_queries() {
    BPlusTree<int> tree;
    std::set<int> reference;
    std::mt19937 generator(0x5EED1234U);
    std::uniform_int_distribution<int> distribution(-50, 250);

    for (int iteration = 0; iteration < 2'000; ++iteration) {
        const int value = distribution(generator);
        const bool actual_inserted = tree.insert(value);
        const bool expected_inserted = reference.insert(value).second;
        expect(
            actual_inserted == expected_inserted,
            "duplicate insertion result should match std::set"
        );
        expect(tree.is_valid(), "invariants should hold after every insertion");
    }

    expect_matches(tree, reference);

    const BPlusTree<int>& const_tree = tree;
    expect(
        const_tree.get(*reference.begin()) == *reference.begin(),
        "const lookup should return an existing key"
    );
    bool missing_lookup_threw = false;
    try {
        static_cast<void>(const_tree.get(10'000));
    } catch (const std::out_of_range&) {
        missing_lookup_threw = true;
    }
    expect(missing_lookup_threw, "const lookup should reject a missing key");

    for (int query = -75; query <= 275; ++query) {
        expect(
            tree.contains(query) == (reference.count(query) == 1),
            "contains should match std::set"
        );

        auto actual_find = tree.find(query);
        const auto expected_find = reference.find(query);
        if (expected_find == reference.end()) {
            expect(actual_find == tree.end(), "find should return end for a missing key");
        } else {
            expect(actual_find != tree.end(), "find should locate an existing key");
            expect(*actual_find == *expected_find, "find should return the expected key");
        }

        auto actual_lower = tree.lower_bound(query);
        const auto expected_lower = reference.lower_bound(query);
        if (expected_lower == reference.end()) {
            expect(actual_lower == tree.end(), "lower_bound should return end when appropriate");
        } else {
            expect(actual_lower != tree.end(), "lower_bound should locate the expected key");
            expect(*actual_lower == *expected_lower, "lower_bound should match std::set");
        }

        auto actual_upper = tree.upper_bound(query);
        const auto expected_upper = reference.upper_bound(query);
        if (expected_upper == reference.end()) {
            expect(actual_upper == tree.end(), "upper_bound should return end when appropriate");
        } else {
            expect(actual_upper != tree.end(), "upper_bound should locate the expected key");
            expect(*actual_upper == *expected_upper, "upper_bound should match std::set");
        }
    }

    BPlusTree<int> copied(tree);
    expect_matches(copied, reference);

    tree.insert(1'000);
    expect(!copied.contains(1'000), "copy construction should produce independent storage");
    expect_matches(copied, reference);

    BPlusTree<int> assigned;
    assigned.insert(-1'000);
    assigned = tree;
    std::set<int> assigned_reference = reference;
    assigned_reference.insert(1'000);
    expect_matches(assigned, assigned_reference);

    tree.insert(2'000);
    expect(!assigned.contains(2'000), "copy assignment should produce independent storage");
    expect_matches(assigned, assigned_reference);

}

void test_empty_iteration_and_shuffled_erase() {
    BPlusTree<int> empty;
    expect(empty.begin() == empty.end(), "an empty tree should have an empty range");

    BPlusTree<int> tree;
    std::set<int> reference;
    std::mt19937 generator(0xE2A5E123U);
    std::uniform_int_distribution<int> distribution(-500, 500);
    for (int iteration = 0; iteration < 2'000; ++iteration) {
        const int value = distribution(generator);
        tree.insert(value);
        reference.insert(value);
    }
    expect_matches(tree, reference);

    std::vector<int> removal_order(reference.begin(), reference.end());
    std::shuffle(removal_order.begin(), removal_order.end(), generator);
    for (const int value : removal_order) {
        expect(tree.erase(value), "existing key erase should succeed");
        reference.erase(value);
        expect_matches(tree, reference);
        expect(!tree.erase(value), "missing key erase should fail without mutation");
    }

    expect(tree.empty(), "erasing every key should leave an empty tree");
    expect(tree.begin() == tree.end(), "erased tree should have an empty range");
}

} // namespace

int main() {
    try {
        test_fixed_seed_insertions_and_queries();
        test_empty_iteration_and_shuffled_erase();
        std::cout << "PASS: deterministic B+ tree evidence" << std::endl;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << std::endl;
        return 1;
    }
}
