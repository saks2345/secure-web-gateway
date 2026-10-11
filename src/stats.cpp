
#include <iostream>
#include <sqlite3.h>

using namespace std;

void runQuery(sqlite3* db, const char* title, const char* sql) {
    cout << "\n=== " << title << " ===\n";

    char* error = nullptr;

    int rc = sqlite3_exec(
        db,
        sql,
        [](void*, int columns, char** values, char** names) -> int {
            for (int i = 0; i < columns; i++) {
                cout << names[i] << ": "
                     << (values[i] ? values[i] : "NULL") << '\n';
            }
            cout << "--------------------\n";
            return 0;
        },
        nullptr,
        &error
    );

    if (rc != SQLITE_OK) {
        cerr << "Query failed: "
             << (error ? error : "unknown error") << '\n';
        sqlite3_free(error);
    }
}

int main() {
    sqlite3* db = nullptr;

    if (sqlite3_open("logs.db", &db) != SQLITE_OK) {
        cerr << "Cannot open logs.db\n";
        if (db) sqlite3_close(db);
        return 1;
    }

    runQuery(db, "TOTAL REQUESTS",
        "SELECT COUNT(*) AS total_requests FROM requests;");

    runQuery(db, "REQUESTS BY ACTION",
        "SELECT action, COUNT(*) AS count "
        "FROM requests GROUP BY action;");

    runQuery(db, "TOP BLOCKED DOMAINS",
        "SELECT host, COUNT(*) AS blocked_count "
        "FROM requests WHERE action = 'BLOCKED' "
        "GROUP BY host ORDER BY blocked_count DESC LIMIT 10;");

    runQuery(db, "AVERAGE LATENCY",
        "SELECT ROUND(AVG(latency_ms), 2) AS average_latency_ms "
        "FROM requests;");

    runQuery(db, "RECENT REQUESTS",
        "SELECT timestamp, client_ip, host, action, "
        "bytes_transferred, ROUND(latency_ms, 2) AS latency_ms "
        "FROM requests ORDER BY id DESC LIMIT 10;");

    sqlite3_close(db);
    return 0;
}
