cmake_minimum_required(VERSION 3.20)

project(SQLInCPP VERSION 1.0.0 LANGUAGES CXX)

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
    includes/table/table.cpp
    includes/tokenizer/ftokenize.cpp
    includes/tokenizer/state_machine_functions.cpp
    includes/tokenizer/stokenize.cpp
)

add_library(sql_core STATIC ${SQL_CORE_SOURCES})
target_include_directories(sql_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

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
endif()
