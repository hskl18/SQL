#include "includes/storage/storage_v2.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct ProcessResult {
    int status;
    std::string output;
};

ProcessResult run_sql_cli(
    const std::string& input,
    const std::string& crash_point = "",
    const std::string& trace_path = ""
) {
    std::array<int, 2> input_pipe{};
    std::array<int, 2> output_pipe{};
    if (::pipe(input_pipe.data()) != 0 || ::pipe(output_pipe.data()) != 0) {
        throw std::runtime_error("pipe failed");
    }
    const auto process = ::fork();
    if (process < 0) throw std::runtime_error("fork failed");
    if (process == 0) {
        ::close(input_pipe[1]);
        ::close(output_pipe[0]);
        ::dup2(input_pipe[0], STDIN_FILENO);
        ::dup2(output_pipe[1], STDOUT_FILENO);
        ::dup2(output_pipe[1], STDERR_FILENO);
        ::close(input_pipe[0]);
        ::close(output_pipe[1]);
        if (!crash_point.empty()) ::setenv("SQL_CRASH_AT", crash_point.c_str(), 1);
        if (!trace_path.empty()) ::setenv("SQL_CRASH_TRACE_FILE", trace_path.c_str(), 1);
        ::execl(RUN_SQL_PATH, RUN_SQL_PATH, nullptr);
        ::_exit(127);
    }

    ::close(input_pipe[0]);
    ::close(output_pipe[1]);
    const auto written = ::write(input_pipe[1], input.data(), input.size());
    ::close(input_pipe[1]);
    if (written != static_cast<ssize_t>(input.size())) {
        ::close(output_pipe[0]);
        throw std::runtime_error("unable to send SQL input");
    }

    std::string output;
    std::array<char, 1024> buffer{};
    while (true) {
        const auto count = ::read(output_pipe[0], buffer.data(), buffer.size());
        if (count <= 0) break;
        output.append(buffer.data(), static_cast<std::size_t>(count));
    }
    ::close(output_pipe[0]);
    int status = 0;
    if (::waitpid(process, &status, 0) != process || !WIFEXITED(status)) {
        return {-1, output};
    }
    return {WEXITSTATUS(status), output};
}

std::vector<std::uint8_t> read_bytes(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    input.seekg(0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return bytes;
}

void write_bytes(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void write_v1_slot(std::ofstream& output, const std::string& value) {
    std::array<char, 100> slot{};
    std::copy(value.begin(), value.end(), slot.begin());
    output.write(slot.data(), static_cast<std::streamsize>(slot.size()));
}

void write_v1_table(
    const std::string& table,
    const std::vector<std::string>& fields,
    const std::vector<std::vector<std::string>>& rows
) {
    std::ofstream schema(table + "_fields.bin", std::ios::binary | std::ios::trunc);
    write_v1_slot(schema, std::to_string(fields.size()));
    for (const auto& field : fields) write_v1_slot(schema, field);
    schema.close();
    std::ofstream data(table + ".bin", std::ios::binary | std::ios::trunc);
    for (const auto& row : rows) {
        for (const auto& value : row) write_v1_slot(data, value);
    }
}

int crash_value(
    const std::string& point,
    const std::string& path,
    const std::string& value
) {
    const auto process = ::fork();
    if (process < 0) throw std::runtime_error("fork failed");
    if (process == 0) {
        ::setenv("SQL_CRASH_AT", point.c_str(), 1);
        ::execl(
            STORAGE_CRASH_WORKER_PATH,
            STORAGE_CRASH_WORKER_PATH,
            path.c_str(),
            value.c_str(),
            nullptr
        );
        ::_exit(127);
    }
    int status = 0;
    if (::waitpid(process, &status, 0) != process) throw std::runtime_error("waitpid failed");
    if (!WIFEXITED(status)) return -1;
    return WEXITSTATUS(status);
}

int crash_insert(const std::string& point, const std::string& path) {
    return crash_value(point, path, "new");
}

int crash_create(const std::string& point, const std::string& path) {
    const auto process = ::fork();
    if (process < 0) throw std::runtime_error("fork failed");
    if (process == 0) {
        ::setenv("SQL_CRASH_AT", point.c_str(), 1);
        ::execl(
            STORAGE_CRASH_WORKER_PATH,
            STORAGE_CRASH_WORKER_PATH,
            path.c_str(),
            "__create__",
            nullptr
        );
        ::_exit(127);
    }
    int status = 0;
    if (::waitpid(process, &status, 0) != process) throw std::runtime_error("waitpid failed");
    if (!WIFEXITED(status)) return -1;
    return WEXITSTATUS(status);
}

std::vector<std::string> trace_points(
    const std::string& path,
    const std::string& value,
    const std::string& trace_path
) {
    std::filesystem::remove(trace_path);
    const auto process = ::fork();
    if (process < 0) throw std::runtime_error("fork failed");
    if (process == 0) {
        ::setenv("SQL_CRASH_TRACE_FILE", trace_path.c_str(), 1);
        ::execl(
            STORAGE_CRASH_WORKER_PATH,
            STORAGE_CRASH_WORKER_PATH,
            path.c_str(),
            value.c_str(),
            nullptr
        );
        ::_exit(127);
    }
    int status = 0;
    if (::waitpid(process, &status, 0) != process || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0) {
        throw std::runtime_error("trace worker failed");
    }
    std::ifstream input(trace_path);
    std::vector<std::string> points;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty()) points.push_back(line);
    }
    std::filesystem::remove(trace_path);
    expect(!points.empty(), "trace should enumerate crash boundaries");
    expect(std::set<std::string>(points.begin(), points.end()).size() == points.size(),
           "one statement should expose each named boundary once");
    return points;
}

std::vector<std::string> trace_public_create_points(
    const std::string& table,
    const std::string& trace_path
) {
    std::filesystem::remove(trace_path);
    const auto traced = run_sql_cli(
        "create table " + table + " fields value\nend\n",
        "",
        trace_path
    );
    expect(traced.status == 0, "public CREATE trace process should exit normally");
    expect(traced.output.find("Error:") == std::string::npos,
           "public CREATE trace should succeed: " + traced.output);
    std::ifstream input(trace_path);
    std::vector<std::string> points;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty()) points.push_back(line);
    }
    std::filesystem::remove(trace_path);
    expect(!points.empty(), "public CREATE trace should enumerate crash boundaries");
    expect(std::set<std::string>(points.begin(), points.end()).size() == points.size(),
           "public CREATE should expose each crash boundary once");
    return points;
}

void cleanup(const std::string& path) {
    std::filesystem::remove(path);
    std::filesystem::remove(storage_v2::wal_path(path));
}

void cleanup_public_table(const std::string& table) {
    cleanup(storage_v2::database_path(table));
    std::filesystem::remove(table + ".bin");
    std::filesystem::remove(table + "_fields.bin");
}

void install_baseline(
    const std::string& path,
    const std::vector<std::uint8_t>& image
) {
    cleanup(path);
    write_bytes(path, image);
    std::ofstream wal(storage_v2::wal_path(path), std::ios::binary | std::ios::trunc);
}

void test_crash_matrix() {
    const std::string probe = "crash-trace-probe.sql2";
    cleanup(probe);
    storage_v2::create_from_rows(probe, {"value"}, {{"old"}});
    const auto points = trace_points(probe, "new", "insert-crash-points.txt");
    cleanup(probe);
    for (const auto& point : points) {
        const auto path = "crash-" + point + ".sql2";
        cleanup(path);
        storage_v2::create_from_rows(path, {"value"}, {{"old"}});
        const auto old_image = read_bytes(path);
        expect(crash_insert(point, path) == 86, "worker should terminate at " + point);

        if (point == "wal-fsync") {
            const auto wal = storage_v2::wal_path(path);
            const auto wal_size = std::filesystem::file_size(wal);
            const auto inspection = storage_v2::inspect(path);
            expect(inspection.wal_present, "inspection should report pending committed WAL");
            expect(std::filesystem::file_size(wal) == wal_size, "inspection must not clear the WAL");
            expect(read_bytes(path) == old_image, "inspection must not apply pending pages");
        }

        const auto recovered = storage_v2::open_table(path);
        const auto old_state = std::vector<std::vector<std::string>>({{"old"}});
        const auto new_state = std::vector<std::vector<std::string>>({{"old"}, {"new"}});
        expect(recovered.rows == old_state || recovered.rows == new_state,
               "crash recovery should expose only the old or new complete state: " + point);
        if (point != "wal-write") {
            expect(recovered.rows == new_state,
                   "durably committed crash point should recover the new state: " + point);
        }
        expect(
            std::filesystem::file_size(storage_v2::wal_path(path)) == 0,
            "recovery should clear WAL: " + point
        );
        const auto recovered_image = read_bytes(path);
        const auto second = storage_v2::open_table(path);
        expect(second.rows == recovered.rows, "second recovery should be idempotent: " + point);
        expect(read_bytes(path) == recovered_image, "second recovery should not rewrite data: " + point);
        cleanup(path);
    }
}

void test_partial_wal_rejection() {
    const std::string path = "crash-partial.sql2";
    cleanup(path);
    storage_v2::create_from_rows(path, {"value"}, {{"old"}});
    const auto old_image = read_bytes(path);
    expect(crash_insert("wal-partial", path) == 86, "partial WAL worker should terminate");
    bool rejected = false;
    try {
        static_cast<void>(storage_v2::open_table(path));
    } catch (const storage_v2::CorruptionError& error) {
        rejected = std::string(error.what()).find("WAL") != std::string::npos;
    }
    expect(rejected, "partial WAL should fail closed explicitly");
    expect(read_bytes(path) == old_image, "partial WAL must leave committed database unchanged");
    cleanup(path);
}

void test_internal_split_crash_matrix() {
    const std::string baseline_path = "deep-crash-baseline.sql2";
    cleanup(baseline_path);
    std::vector<std::vector<std::string>> rows;
    for (int record = 0; record < 1'296; ++record) {
        auto value = std::to_string(record);
        value.insert(value.begin(), 100 - value.size(), '0');
        rows.push_back({value});
    }
    storage_v2::create_from_rows(baseline_path, {"value"}, rows);
    const auto baseline = read_bytes(baseline_path);
    const auto inserted_value = std::string(100, 'z');

    const std::string probe = "deep-crash-probe.sql2";
    install_baseline(probe, baseline);
    const auto points = trace_points(probe, inserted_value, "deep-crash-points.txt");
    cleanup(probe);
    expect(std::count_if(points.begin(), points.end(), [](const std::string& point) {
        return point.rfind("page-", 0) == 0;
    }) >= 7, "internal split trace should expose every dirty path and split page");

    for (const auto& point : points) {
        const auto path = "deep-crash-" + point + ".sql2";
        install_baseline(path, baseline);
        expect(crash_value(point, path, inserted_value) == 86,
               "deep split worker should terminate at " + point);
        const auto recovered = storage_v2::open_table(path);
        expect(recovered.rows.size() == rows.size() || recovered.rows.size() == rows.size() + 1,
               "deep crash should expose only the old or new row count: " + point);
        if (point != "wal-write") {
            expect(recovered.rows.size() == rows.size() + 1,
                   "durable deep split should recover the inserted row: " + point);
        }
        expect(recovered.indexes[0].size() == recovered.rows.size(),
               "deep split recovery should keep index and row counts equal: " + point);
        cleanup(path);
    }
    cleanup(baseline_path);
}

void test_create_crash_matrix() {
    const std::string probe = "create-crash-trace-probe.sql2";
    cleanup(probe);
    const auto points = trace_points(probe, "__create__", "create-crash-points.txt");
    cleanup(probe);
    for (const auto& point : points) {
        const auto path = "create-crash-" + point + ".sql2";
        cleanup(path);
        expect(crash_create(point, path) == 86, "create worker should terminate at " + point);
        const auto recovered = storage_v2::open_table(path);
        expect(recovered.fields == std::vector<std::string>({"value"}),
               "recovered create should contain complete schema: " + point);
        expect(recovered.rows.empty(), "recovered create should contain no partial row: " + point);
        cleanup(path);
    }
}

void test_public_orphan_create_wal_recovers_on_select() {
    const std::string table = "public_create_wal_fsync";
    const auto path = storage_v2::database_path(table);
    cleanup(path);
    const auto crashed = run_sql_cli(
        "create table " + table + " fields value\nend\n",
        "wal-fsync"
    );
    expect(crashed.status == 86, "public CREATE should terminate at wal-fsync");
    expect(!std::filesystem::exists(path), "wal-fsync CREATE should not yet have a data file");
    expect(std::filesystem::file_size(storage_v2::wal_path(path)) > 0,
           "wal-fsync CREATE should leave a committed WAL");

    const auto selected = run_sql_cli("select * from " + table + "\nend\n");
    expect(selected.status == 0, "public SELECT process should exit normally");
    expect(selected.output.find("Error:") == std::string::npos,
           "public SELECT should recover the orphan CREATE WAL: " + selected.output);
    expect(selected.output.find("Selected fields:") != std::string::npos,
           "public SELECT should expose the recovered schema");
    expect(selected.output.find("Record count:") != std::string::npos,
           "public SELECT should print its record result");
    expect(std::filesystem::exists(path), "public SELECT should materialize the database image");
    expect(std::filesystem::file_size(storage_v2::wal_path(path)) == 0,
           "public SELECT recovery should clear the WAL");
    expect(storage_v2::open_table(path).rows.empty(),
           "public SELECT should expose the complete empty table");
    cleanup(path);
}

void test_public_create_crash_matrix() {
    const std::string probe = "public_create_trace_probe";
    cleanup_public_table(probe);
    const auto points = trace_public_create_points(probe, "public-create-crash-points.txt");
    cleanup_public_table(probe);
    expect(std::find(points.begin(), points.end(), "wal-write") != points.end(),
           "public CREATE matrix should cover WAL write");
    expect(std::find(points.begin(), points.end(), "wal-fsync") != points.end(),
           "public CREATE matrix should cover WAL fsync");
    expect(std::find_if(points.begin(), points.end(), [](const std::string& point) {
        return point.rfind("page-", 0) == 0;
    }) != points.end(), "public CREATE matrix should cover page writes");
    expect(std::find(points.begin(), points.end(), "data-fsync") != points.end(),
           "public CREATE matrix should cover data fsync");
    expect(std::find(points.begin(), points.end(), "wal-clear") != points.end(),
           "public CREATE matrix should cover the committed WAL-clear boundary");

    for (const auto& point : points) {
        auto suffix = point;
        std::replace(suffix.begin(), suffix.end(), '-', '_');
        const auto table = "public_create_" + suffix;
        const auto path = storage_v2::database_path(table);
        cleanup_public_table(table);
        const auto crashed = run_sql_cli(
            "create table " + table + " fields value\nend\n",
            point
        );
        expect(crashed.status == 86, "public CREATE should terminate at " + point);

        auto selected = run_sql_cli("select * from " + table + "\nend\n");
        if (selected.output.find("Table does not exist") != std::string::npos) {
            const auto retried = run_sql_cli(
                "create table " + table + " fields value\nend\n"
            );
            expect(retried.output.find("Error:") == std::string::npos,
                   "old-state CREATE retry should complete recovery at " + point +
                       ": " + retried.output);
            selected = run_sql_cli("select * from " + table + "\nend\n");
        }
        expect(selected.status == 0, "public SELECT process should exit normally at " + point);
        expect(selected.output.find("Error:") == std::string::npos,
               "public path should expose an old-or-new recoverable state at " + point +
                   ": " + selected.output);
        expect(selected.output.find("Selected fields: value") != std::string::npos,
               "public SELECT should expose the complete schema at " + point);
        expect(std::filesystem::exists(path),
               "public recovery should materialize the database at " + point);
        expect(std::filesystem::file_size(storage_v2::wal_path(path)) == 0,
               "public recovery should clear the WAL at " + point);
        expect(storage_v2::open_table(path).rows.empty(),
               "public recovery should expose no partial rows at " + point);
        cleanup_public_table(table);
    }
}

void test_public_orphan_wal_ownership_boundaries() {
    const std::string owner = "public_wal_owner";
    const std::string other = "public_wal_other";
    cleanup_public_table(owner);
    cleanup_public_table(other);
    const auto crashed = run_sql_cli(
        "create table " + owner + " fields value\nend\n",
        "wal-fsync"
    );
    expect(crashed.status == 86, "owner CREATE should leave an orphan WAL");
    const auto unrelated = run_sql_cli("select * from " + other + "\nend\n");
    expect(unrelated.output.find("Table does not exist: " + other) != std::string::npos,
           "an orphan WAL must not identify a different table");
    expect(!std::filesystem::exists(storage_v2::database_path(owner)),
           "opening another table must not recover the owner's WAL");
    expect(!std::filesystem::exists(storage_v2::database_path(other)),
           "opening another table must not create a database");
    const auto owner_selected = run_sql_cli("select * from " + owner + "\nend\n");
    expect(owner_selected.output.find("Error:") == std::string::npos,
           "the exact owner path should recover its WAL");
    cleanup_public_table(owner);
    cleanup_public_table(other);

    const std::string legacy = "public_wal_legacy";
    cleanup_public_table(legacy);
    const auto legacy_crash = run_sql_cli(
        "create table " + legacy + " fields value\nend\n",
        "wal-fsync"
    );
    expect(legacy_crash.status == 86, "legacy boundary should leave an orphan WAL");
    const auto wal_before = read_bytes(storage_v2::wal_path(storage_v2::database_path(legacy)));
    write_v1_table(legacy, {"value"}, {{"legacy"}});
    const auto v1_data_before = read_bytes(legacy + ".bin");
    const auto v1_schema_before = read_bytes(legacy + "_fields.bin");
    const auto legacy_selected = run_sql_cli("select * from " + legacy + "\nend\n");
    expect(legacy_selected.output.find("Error:") == std::string::npos,
           "valid v1 should remain publicly readable beside an orphan WAL");
    expect(legacy_selected.output.find("legacy") != std::string::npos,
           "public SELECT should return the v1 row");
    expect(!std::filesystem::exists(storage_v2::database_path(legacy)),
           "orphan WAL recovery must not supersede v1");
    expect(read_bytes(legacy + ".bin") == v1_data_before,
           "orphan WAL recovery must not modify v1 rows");
    expect(read_bytes(legacy + "_fields.bin") == v1_schema_before,
           "orphan WAL recovery must not modify v1 schema");
    expect(read_bytes(storage_v2::wal_path(storage_v2::database_path(legacy))) == wal_before,
           "opening v1 must leave the conflicting orphan WAL untouched");
    cleanup_public_table(legacy);
}

void test_wal_checksum_rejection() {
    const std::string path = "crash-wal-checksum.sql2";
    cleanup(path);
    storage_v2::create_from_rows(path, {"value"}, {{"old"}});
    const auto old_image = read_bytes(path);
    expect(crash_insert("wal-fsync", path) == 86, "WAL fixture worker should terminate");
    const auto wal_path = storage_v2::wal_path(path);
    auto wal = read_bytes(wal_path);
    expect(wal.size() > 64, "committed WAL fixture should contain an image");
    wal[48] ^= 0x7fU;
    {
        std::ofstream output(wal_path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(wal.data()), static_cast<std::streamsize>(wal.size()));
    }
    bool rejected = false;
    try {
        static_cast<void>(storage_v2::open_table(path));
    } catch (const storage_v2::CorruptionError& error) {
        rejected = std::string(error.what()).find("checksum") != std::string::npos;
    }
    expect(rejected, "corrupt committed WAL checksum should fail closed");
    expect(read_bytes(path) == old_image, "corrupt WAL must not modify the database");
    cleanup(path);
}

void test_stale_wal_rejection() {
    const std::string path = "crash-stale-wal.sql2";
    cleanup(path);
    storage_v2::create_from_rows(path, {"value"}, {{"old"}});
    expect(crash_insert("wal-fsync", path) == 86, "stale WAL fixture should terminate");
    const auto stale_wal = read_bytes(storage_v2::wal_path(path));
    static_cast<void>(storage_v2::open_table(path));
    storage_v2::insert_row(path, {"newer"});
    write_bytes(storage_v2::wal_path(path), stale_wal);
    bool inspect_rejected = false;
    try {
        static_cast<void>(storage_v2::inspect(path));
    } catch (const storage_v2::CorruptionError& error) {
        inspect_rejected = std::string(error.what()).find("stale WAL") != std::string::npos;
    }
    expect(inspect_rejected, "inspection should reject a rollback WAL");
    bool recovery_rejected = false;
    try {
        static_cast<void>(storage_v2::open_table(path));
    } catch (const storage_v2::CorruptionError& error) {
        recovery_rejected = std::string(error.what()).find("stale WAL") != std::string::npos;
    }
    expect(recovery_rejected, "normal recovery should reject the same rollback WAL");
    cleanup(path);
}

} // namespace

int main() {
    try {
        test_crash_matrix();
        std::cout << "PASS: deterministic committed crash matrix and idempotent recovery\n";
        test_partial_wal_rejection();
        std::cout << "PASS: partial WAL rejection\n";
        test_internal_split_crash_matrix();
        std::cout << "PASS: dynamic internal-split crash matrix\n";
        test_create_crash_matrix();
        std::cout << "PASS: atomic create crash matrix\n";
        test_public_orphan_create_wal_recovers_on_select();
        std::cout << "PASS: public orphan CREATE WAL recovery\n";
        test_public_create_crash_matrix();
        std::cout << "PASS: public CREATE crash matrix\n";
        test_public_orphan_wal_ownership_boundaries();
        std::cout << "PASS: public orphan WAL ownership boundaries\n";
        test_wal_checksum_rejection();
        std::cout << "PASS: WAL checksum rejection\n";
        test_stale_wal_rejection();
        std::cout << "PASS: stale WAL rejection in inspection and recovery\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
