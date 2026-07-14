#include "includes/storage/storage_v2.h"

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

void cleanup(const std::string& path) {
    std::filesystem::remove(path);
    std::filesystem::remove(storage_v2::wal_path(path));
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
