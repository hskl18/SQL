#include "includes/sql/sql.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void expect_equal(long actual, long expected, const std::string& message) {
    if (actual != expected) {
        throw std::runtime_error(
            message + ": expected " + std::to_string(expected) +
            ", received " + std::to_string(actual)
        );
    }
}

void expect_true(bool value, const std::string& message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void expect_records(
    const std::vector<long>& actual,
    const std::vector<long>& expected,
    const std::string& message
) {
    if (actual != expected) {
        throw std::runtime_error(message);
    }
}

void remove_table(const std::string& name) {
    std::filesystem::remove(name + ".bin");
    std::filesystem::remove(name + "_fields.bin");
}

void test_batch_executes_repository_fixture() {
    remove_table("employeeSample01");
    remove_table("studentSample01");

    SQL sql;
    const std::string fixture =
        std::string(SQL_TEST_FIXTURE_DIR) + "/_!sample01.txt";
    sql.batch(fixture.c_str());

    const Table employees = sql.command("select * from employeeSample01");
    expect_equal(employees.record_count(), 6, "sample batch employee count");

    const Table students = sql.command(
        "select * from studentSample01 where lname = \"Del Rio\""
    );
    expect_records(students.select_recnos(), {3}, "quoted batch selection");

    remove_table("employeeSample01");
    remove_table("studentSample01");
}

void test_commands_persist_and_filter_records() {
    remove_table("people_test");

    SQL sql;
    sql.command("create table people_test fields first, last, major, age");
    sql.command("insert into people_test values Ada, Lovelace, Math, 36");
    sql.command("insert into people_test values Grace, Hopper, CS, 30");
    sql.command("insert into people_test values Edsger, Dijkstra, CS, 40");
    sql.command("insert into people_test values Barbara, Liskov, CS, 24");

    const Table all = sql.command("select * from people_test");
    expect_equal(all.record_count(), 4, "inserted record count");

    const Table filtered = sql.command(
        "select first, major from people_test where major = CS and age < 35"
    );
    expect_records(filtered.select_recnos(), {1, 3}, "compound selection");
    expect_true(
        filtered.get_fields() == std::vector<std::string>({"first", "major"}),
        "selected fields should be preserved"
    );

    SQL reopened;
    const Table persisted = reopened.command(
        "select * from people_test where age >= 36"
    );
    expect_records(persisted.select_recnos(), {0, 2}, "persisted selection");

    reopened.command("drop table people_test");
    expect_true(reopened.is_error(), "invalid commands should report an error");

    remove_table("people_test");
}

void test_second_batch_fixture_and_missing_file_errors() {
    remove_table("employeeSample02");
    remove_table("studentSelect02");

    SQL sql;
    const std::string fixture =
        std::string(SQL_TEST_FIXTURE_DIR) + "/_!sample02.txt";
    sql.batch(fixture.c_str());

    const Table employees = sql.command("select * from employeeSample02");
    expect_equal(employees.record_count(), 16, "second batch employee count");

    const Table students = sql.command(
        "select * from studentSelect02 where major = CS and age < 25"
    );
    expect_records(students.select_recnos(), {0, 7}, "second batch filter");

    SQL missing;
    missing.batch("missing-fixture.sql");
    expect_true(missing.is_error(), "missing batch files should report an error");

    remove_table("employeeSample02");
    remove_table("studentSelect02");
}

} // namespace

int main() {
    try {
        test_commands_persist_and_filter_records();
        std::cout << "PASS: command persistence and filtering" << std::endl;
        test_batch_executes_repository_fixture();
        std::cout << "PASS: repository batch fixture" << std::endl;
        test_second_batch_fixture_and_missing_file_errors();
        std::cout << "PASS: second batch fixture and errors" << std::endl;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << std::endl;
        return 1;
    }
}
