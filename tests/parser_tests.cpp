#include "includes/parser/parser.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void expect_true(bool value, const std::string& message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void expect_values(
    MMap<std::string, std::string>& tree,
    const std::string& key,
    const std::vector<std::string>& expected,
    const std::string& message
) {
    if (!tree.contains(key) || tree.at(key) != expected) {
        throw std::runtime_error(message);
    }
}

void test_quoted_literal_preserves_spaces_numbers_and_punctuation() {
    Parser parser("insert into rooms values \"Room 101, West\"");
    auto tree = parser.parse_tree();

    expect_values(
        tree,
        "values",
        {"Room 101, West"},
        "quoted insert value should remain one literal"
    );
}

void test_quoted_keywords_remain_one_literal() {
    Parser parser("insert into rooms values \"select from where, 101!\"");
    auto tree = parser.parse_tree();

    expect_values(
        tree,
        "values",
        {"select from where, 101!"},
        "quoted keywords and punctuation should remain literal text"
    );
}

void test_unmatched_quote_is_rejected() {
    Parser parser("insert into rooms values \"Room 101, West");
    auto tree = parser.parse_tree();

    expect_true(tree.empty(), "an unmatched quote should reject the statement");
    expect_true(parser.get_input().empty(), "an unmatched quote should discard partial tokens");
}

void test_unknown_unquoted_character_is_rejected() {
    Parser parser("insert into rooms values Room@");
    auto tree = parser.parse_tree();

    expect_true(tree.empty(), "an unknown unquoted character should reject the statement");
    expect_true(
        parser.get_input().empty(),
        "an unknown unquoted character should discard partial tokens"
    );
}

void test_existing_select_grammar_accepts_a_quoted_condition() {
    Parser parser("select name from rooms where label = \"Room 101, West\"");
    auto tree = parser.parse_tree();

    expect_values(tree, "command", {"select"}, "select command should remain supported");
    expect_values(tree, "fields", {"name"}, "select field should remain supported");
    expect_values(tree, "table_name", {"rooms"}, "select table should remain supported");
    expect_values(
        tree,
        "condition",
        {"label", "=", "Room 101, West"},
        "quoted condition should remain one literal"
    );
}

} // namespace

int main() {
    try {
        test_quoted_literal_preserves_spaces_numbers_and_punctuation();
        test_quoted_keywords_remain_one_literal();
        test_unmatched_quote_is_rejected();
        test_unknown_unquoted_character_is_rejected();
        test_existing_select_grammar_accepts_a_quoted_condition();
        std::cout << "PASS: parser quote and invalid-token handling" << std::endl;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << std::endl;
        return 1;
    }
}
