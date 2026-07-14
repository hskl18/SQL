cmake_minimum_required(VERSION 3.20)

project(SQLInCPP VERSION 2.0.0 LANGUAGES CXX)

if(NOT UNIX)
    message(FATAL_ERROR "Storage format v2 requires POSIX file and process primitives")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SQL_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(SQL_ENABLE_SANITIZERS)
    if(MSVC)
        message(FATAL_ERROR "SQL_ENABLE_SANITIZERS requires GCC or Clang")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

set(SQL_CORE_SOURCES
    includes/binary_files/file_record.cpp
    includes/binary_files/utilities.cpp
    includes/parser/parser.cpp
    includes/shunting_yard/shunting_yard.cpp
    includes/sql/sql.cpp
    includes/storage/page_buffer.cpp
    includes/storage/storage_v2.cpp
    includes/table/table.cpp
    includes/tokenizer/ftokenize.cpp
    includes/tokenizer/state_machine_functions.cpp
    includes/tokenizer/stokenize.cpp
)

add_library(sql_core STATIC ${SQL_CORE_SOURCES})
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/includes/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/version.h"
    @ONLY
)
target_include_directories(sql_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

function(sql_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

sql_enable_warnings(sql_core)

add_executable(run_sql run_sql.cpp)
target_link_libraries(run_sql PRIVATE sql_core)
sql_enable_warnings(run_sql)

add_executable(sql_migrate_v1 tools/sql_migrate_v1.cpp)
target_link_libraries(sql_migrate_v1 PRIVATE sql_core)
sql_enable_warnings(sql_migrate_v1)

include(CTest)

if(BUILD_TESTING)
    add_executable(sql_tests tests/sql_tests.cpp)
    target_link_libraries(sql_tests PRIVATE sql_core)
    sql_enable_warnings(sql_tests)
    target_compile_definitions(sql_tests PRIVATE
        SQL_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/batch"
    )

    set(SQL_TEST_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-workspace")
    file(MAKE_DIRECTORY "${SQL_TEST_WORKING_DIRECTORY}")

    add_test(NAME sql_behavior COMMAND sql_tests)
    set_tests_properties(sql_behavior PROPERTIES
        WORKING_DIRECTORY "${SQL_TEST_WORKING_DIRECTORY}"
    )

    add_executable(parser_tests tests/parser_tests.cpp)
    target_link_libraries(parser_tests PRIVATE sql_core)
    sql_enable_warnings(parser_tests)
    add_test(NAME parser_safety COMMAND parser_tests)

    add_executable(bplustree_tests tests/bplustree_tests.cpp)
    target_link_libraries(bplustree_tests PRIVATE sql_core)
    sql_enable_warnings(bplustree_tests)
    add_test(NAME bplustree_evidence COMMAND bplustree_tests)

    add_executable(storage_v2_tests tests/storage_v2_tests.cpp)
    target_link_libraries(storage_v2_tests PRIVATE sql_core)
    sql_enable_warnings(storage_v2_tests)
    add_test(NAME storage_v2_integrity COMMAND storage_v2_tests)

    add_executable(storage_crash_worker tests/storage_crash_worker.cpp)
    target_link_libraries(storage_crash_worker PRIVATE sql_core)
    sql_enable_warnings(storage_crash_worker)

    add_executable(storage_crash_tests tests/storage_crash_tests.cpp)
    target_link_libraries(storage_crash_tests PRIVATE sql_core)
    target_compile_definitions(storage_crash_tests PRIVATE
        STORAGE_CRASH_WORKER_PATH="$<TARGET_FILE:storage_crash_worker>"
    )
    add_dependencies(storage_crash_tests storage_crash_worker)
    sql_enable_warnings(storage_crash_tests)
    add_test(NAME storage_crash_recovery COMMAND storage_crash_tests)
    set_tests_properties(storage_crash_recovery PROPERTIES TIMEOUT 60 LABELS crash)

    add_executable(migration_tests tests/migration_tests.cpp)
    target_link_libraries(migration_tests PRIVATE sql_core)
    target_compile_definitions(migration_tests PRIVATE
        MIGRATION_TOOL_PATH="$<TARGET_FILE:sql_migrate_v1>"
    )
    add_dependencies(migration_tests sql_migrate_v1)
    sql_enable_warnings(migration_tests)
    add_test(NAME migration_v1_equivalence COMMAND migration_tests)

    add_executable(cli_tests tests/cli_tests.cpp)
    target_link_libraries(cli_tests PRIVATE sql_core)
    target_compile_definitions(cli_tests PRIVATE
        RUN_SQL_PATH="$<TARGET_FILE:run_sql>"
    )
    add_dependencies(cli_tests run_sql)
    sql_enable_warnings(cli_tests)
    add_test(NAME cli_storage_inspection COMMAND cli_tests)

    add_test(NAME cli_version COMMAND run_sql --version)
    set_tests_properties(cli_version PROPERTIES
        PASS_REGULAR_EXPRESSION "SQL in C\\+\\+ 2.0.0 \\(storage format 2\\)"
    )
    add_test(NAME migration_cli_version COMMAND sql_migrate_v1 --version)
    set_tests_properties(migration_cli_version PROPERTIES
        PASS_REGULAR_EXPRESSION "sql_migrate_v1 2.0.0 \\(storage format 2\\)"
    )
endif()
