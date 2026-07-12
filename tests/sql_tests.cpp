#include "includes/sql/sql.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
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

void expect_error(SQL& sql, const std::string& message_fragment) {
    expect_true(sql.is_error(), "command should fail");
    expect_true(
        sql.last_error().find(message_fragment) != std::string::npos,
        "error should contain: " + message_fragment + ", received: " +
            sql.last_error()
    );
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

void test_command_failures_do_not_terminate_or_mutate_storage() {
    remove_table("integrity_test");

    SQL missing;
    missing.command("select * from missing_table");
    expect_error(missing, "does not exist");

    SQL sql;
    sql.command("create table integrity_test fields first, last");
    expect_true(!sql.is_error(), "valid create should succeed");
    const auto empty_size = std::filesystem::file_size("integrity_test.bin");

    sql.command("insert into integrity_test values only_one");
    expect_error(sql, "Expected 2 values, received 1");
    expect_equal(
        static_cast<long>(std::filesystem::file_size("integrity_test.bin")),
        static_cast<long>(empty_size),
        "short insert must not change storage"
    );

    sql.command("insert into integrity_test values one, two, three");
    expect_error(sql, "Expected 2 values, received 3");
    expect_equal(
        static_cast<long>(std::filesystem::file_size("integrity_test.bin")),
        static_cast<long>(empty_size),
        "long insert must not change storage"
    );

    sql.command("insert into integrity_test values Ada, Lovelace");
    expect_true(!sql.is_error(), "valid insert should succeed after errors");
    expect_equal(
        static_cast<long>(std::filesystem::file_size("integrity_test.bin")),
        200,
        "two-field row should use two fixed-width slots"
    );
    {
        std::ifstream stored("integrity_test.bin", std::ios::binary);
        std::vector<char> bytes(200);
        stored.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        expect_true(stored.gcount() == 200, "stored row should be complete");
        expect_true(
            std::all_of(bytes.begin() + 3, bytes.begin() + 100, [](char byte) {
                return byte == '\0';
            }),
            "short values should be deterministically zero padded"
        );
    }

    sql.command("create table integrity_test fields replacement");
    expect_error(sql, "already exists");
    SQL reopened;
    const Table persisted = reopened.command("select * from integrity_test");
    expect_true(!reopened.is_error(), "existing data should survive rejected create");
    expect_equal(persisted.record_count(), 1, "rejected create preserves rows");

    Table opened("integrity_test");
    Table assigned;
    assigned = opened;
    const Table assigned_projection = assigned.select({"first"});
    expect_true(
        assigned_projection.get_fields() == std::vector<std::string>({"first"}),
        "table assignment should preserve field lookup metadata"
    );

    sql.command("select unknown from integrity_test");
    expect_error(sql, "Unknown selected field");
    sql.command("select * from integrity_test where unknown = Ada");
    expect_error(sql, "Unknown field");

    remove_table("integrity_test");
}

void test_schema_value_and_corrupt_file_boundaries() {
    remove_table("boundary_test");

    SQL sql;
    sql.command("create table boundary_test fields value, value");
    expect_error(sql, "Duplicate field name");
    expect_true(
        !std::filesystem::exists("boundary_test.bin"),
        "invalid schema must not create a data file"
    );

    sql.command("create table boundary_test fields value");
    expect_true(!sql.is_error(), "valid boundary table should be created");
    const std::string exact_value(100, 'x');
    sql.command("insert into boundary_test values \"" + exact_value + "\"");
    expect_true(!sql.is_error(), "100-byte value should fit the fixed slot");
    const auto valid_size = std::filesystem::file_size("boundary_test.bin");

    const std::string oversized_value(101, 'y');
    sql.command("insert into boundary_test values \"" + oversized_value + "\"");
    expect_error(sql, "must not exceed 100 bytes");
    expect_equal(
        static_cast<long>(std::filesystem::file_size("boundary_test.bin")),
        static_cast<long>(valid_size),
        "oversized value must not change storage"
    );

    {
        std::ofstream corrupt("boundary_test.bin", std::ios::binary | std::ios::app);
        corrupt.put('x');
    }
    SQL corrupt_reader;
    corrupt_reader.command("select * from boundary_test");
    expect_error(corrupt_reader, "partial row");

    remove_table("boundary_test");
}

void test_malformed_filters_fail_closed() {
    remove_table("filter_test");
    SQL sql;
    sql.command("create table filter_test fields value");
    sql.command("insert into filter_test values x");
    sql.command("insert into filter_test values y");

    const Table result = sql.command("select * from filter_test where value = x)");
    expect_error(sql, "parenthesis");
    expect_equal(result.record_count(), 0, "invalid filter must not return all rows");

    bool direct_expression_failed = false;
    try {
        Table opened("filter_test");
        static_cast<void>(opened.select({"value"}, {"value", "="}));
    } catch (const std::invalid_argument&) {
        direct_expression_failed = true;
    }
    expect_true(
        direct_expression_failed,
        "malformed direct expressions should throw instead of underflowing a stack"
    );

    remove_table("filter_test");
}

void test_schema_metadata_size_must_match_exactly() {
    remove_table("metadata_extra_test");
    remove_table("metadata_short_test");

    SQL sql;
    sql.command("create table metadata_extra_test fields value");
    {
        std::ofstream extra(
            "metadata_extra_test_fields.bin",
            std::ios::binary | std::ios::app
        );
        extra.put('x');
    }
    SQL extra_reader;
    extra_reader.command("select * from metadata_extra_test");
    expect_error(extra_reader, "metadata size is invalid");

    sql.command("create table metadata_short_test fields value");
    const auto metadata_size =
        std::filesystem::file_size("metadata_short_test_fields.bin");
    std::filesystem::resize_file(
        "metadata_short_test_fields.bin",
        metadata_size - 1
    );
    SQL short_reader;
    short_reader.command("select * from metadata_short_test");
    expect_error(short_reader, "metadata size is invalid");

    remove_table("metadata_extra_test");
    remove_table("metadata_short_test");
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
        test_command_failures_do_not_terminate_or_mutate_storage();
        std::cout << "PASS: command failures preserve storage" << std::endl;
        test_schema_value_and_corrupt_file_boundaries();
        std::cout << "PASS: schema, value, and corrupt file boundaries" << std::endl;
        test_malformed_filters_fail_closed();
        std::cout << "PASS: malformed filters fail closed" << std::endl;
        test_schema_metadata_size_must_match_exactly();
        std::cout << "PASS: schema metadata size is exact" << std::endl;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << std::endl;
        return 1;
    }
}
