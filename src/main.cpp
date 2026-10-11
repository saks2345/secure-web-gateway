
#include <iostream>
#include <string>
#include <cstring>
#include <sstream>
#include <fstream>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_set>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>

#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <sqlite3.h>

using namespace std;

unordered_set<string> blockedDomains;

sqlite3* db = nullptr;
mutex dbMutex;

// -------------------- DATABASE --------------------

void initDatabase() {
    int rc = sqlite3_open("logs.db", &db);

    if (rc != SQLITE_OK) {
        cerr << "Database error: "
             << (db ? sqlite3_errmsg(db) : "cannot open database")
             << '\n';

        if (db) sqlite3_close(db);
        db = nullptr;
        return;
    }

    const char* sql =
        "CREATE TABLE IF NOT EXISTS requests ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "timestamp TEXT NOT NULL,"
        "client_ip TEXT NOT NULL,"
        "host TEXT NOT NULL,"
        "action TEXT NOT NULL,"
        "bytes_transferred INTEGER NOT NULL,"
        "latency_ms REAL NOT NULL"
        ");";

    char* error = nullptr;

    rc = sqlite3_exec(db, sql, nullptr, nullptr, &error);

    if (rc != SQLITE_OK) {
        cerr << "Database table error: "
             << (error ? error : "unknown error") << '\n';
        sqlite3_free(error);
    } else {
        cout << "SQLite logging ready\n";
    }
}

void logRequest(const string& clientIp,
                const string& host,
                const string& action,
                long long bytes,
                double latencyMs) {
    lock_guard<mutex> lock(dbMutex);

    if (!db) return;

    time_t now = time(nullptr);
    tm utc{};

    gmtime_r(&now, &utc);

    char timestamp[32];
    strftime(timestamp, sizeof(timestamp),
             "%Y-%m-%d %H:%M:%S", &utc);

    const char* sql =
        "INSERT INTO requests "
        "(timestamp, client_ip, host, action, "
        "bytes_transferred, latency_ms) "
        "VALUES (?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        cerr << "Log preparation failed: "
             << sqlite3_errmsg(db) << '\n';
        return;
    }

    sqlite3_bind_text(stmt, 1, timestamp, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, clientIp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, host.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, action.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, bytes);
    sqlite3_bind_double(stmt, 6, latencyMs);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        cerr << "Log write failed: " << sqlite3_errmsg(db) << '\n';
    }

    sqlite3_finalize(stmt);
}

// -------------------- BLOCKLIST --------------------

string normalizeHost(string host) {
    transform(host.begin(), host.end(), host.begin(),
              [](unsigned char c) {
                  return static_cast<char>(tolower(c));
              });

    if (!host.empty() && host.back() == '.') {
        host.pop_back();
    }

    return host;
}

void loadBlocklist() {
    ifstream file("config/blocklist.txt");
    string line;

    if (!file) {
        cerr << "Warning: config/blocklist.txt could not be opened\n";
        return;
    }

    while (getline(file, line)) {
        size_t start = line.find_first_not_of(" \t\r\n");

        if (start == string::npos) continue;

        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);

        if (line[0] == '#') continue;

        blockedDomains.insert(normalizeHost(line));
    }

    cout << "Loaded " << blockedDomains.size()
         << " blocklist rules\n";
}

bool isBlocked(const string& rawHost) {
    string host = normalizeHost(rawHost);

    for (const string& rule : blockedDomains) {
        if (rule.rfind("*.", 0) == 0) {
            string suffix = rule.substr(1);

            if (host.size() > suffix.size() &&
                host.compare(host.size() - suffix.size(),
                             suffix.size(), suffix) == 0) {
                return true;
            }
        } else if (host == rule ||
                   (host.size() > rule.size() &&
                    host.compare(host.size() - rule.size(),
                                 rule.size(), rule) == 0 &&
                    host[host.size() - rule.size() - 1] == '.')) {
            return true;
        }
    }

    return false;
}

bool sendAll(int sock, const char* data, size_t length) {
    size_t sent = 0;

    while (sent < length) {
        ssize_t n = send(sock, data + sent, length - sent, MSG_NOSIGNAL);

        if (n <= 0) return false;

        sent += static_cast<size_t>(n);
    }

    return true;
}

void sendBlockedResponse(int client) {
    const string body =
        "<html><body><h1>403 Forbidden</h1>"
        "<p>Access to this domain is blocked by policy.</p>"
        "</body></html>";

    const string response =
        "HTTP/1.1 403 Forbidden\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Connection: close\r\n"
        "Content-Length: " + to_string(body.size()) + "\r\n\r\n" +
        body;

    sendAll(client, response.c_str(), response.size());
}

// -------------------- DESTINATION CONNECTION --------------------

int connectToHost(const string& host, const string& port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* results = nullptr;

    int status = getaddrinfo(host.c_str(), port.c_str(),
                             &hints, &results);

    if (status != 0) {
        cerr << "DNS lookup failed: " << gai_strerror(status) << '\n';
        return -1;
    }

    int sock = -1;

    for (addrinfo* p = results; p != nullptr; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);

        if (sock < 0) continue;

        if (connect(sock, p->ai_addr, p->ai_addrlen) == 0) {
            break;
        }

        close(sock);
        sock = -1;
    }

    freeaddrinfo(results);
    return sock;
}

// -------------------- HTTPS TUNNEL RELAY --------------------

long long relay(int client, int destination) {
    char buffer[8192];
    long long totalBytes = 0;

    while (true) {
        fd_set readSet;

        FD_ZERO(&readSet);
        FD_SET(client, &readSet);
        FD_SET(destination, &readSet);

        int maxFd = max(client, destination);

        int ready = select(maxFd + 1, &readSet, nullptr, nullptr, nullptr);

        if (ready < 0) break;

        int source = -1;
        int target = -1;

        if (FD_ISSET(client, &readSet)) {
            source = client;
            target = destination;
        } else if (FD_ISSET(destination, &readSet)) {
            source = destination;
            target = client;
        }

        if (source < 0) continue;

        ssize_t n = recv(source, buffer, sizeof(buffer), 0);

        if (n <= 0) break;

        if (!sendAll(target, buffer, static_cast<size_t>(n))) break;

        totalBytes += n;
    }

    return totalBytes;
}

// -------------------- THREAD POOL --------------------

class ThreadPool {
private:
    vector<thread> workers;
    queue<function<void()>> tasks;
    mutex queueMutex;
    condition_variable condition;
    bool stopping = false;

public:
    explicit ThreadPool(size_t threadCount) {
        for (size_t i = 0; i < threadCount; i++) {
            workers.emplace_back([this]() {
                while (true) {
                    function<void()> task;

                    {
                        unique_lock<mutex> lock(queueMutex);

                        condition.wait(lock, [this]() {
                            return stopping || !tasks.empty();
                        });

                        if (stopping && tasks.empty()) return;

                        task = move(tasks.front());
                        tasks.pop();
                    }

                    task();
                }
            });
        }
    }

    void enqueue(function<void()> task) {
        {
            lock_guard<mutex> lock(queueMutex);
            tasks.push(move(task));
        }

        condition.notify_one();
    }

    ~ThreadPool() {
        {
            lock_guard<mutex> lock(queueMutex);
            stopping = true;
        }

        condition.notify_all();

        for (thread& worker : workers) {
            if (worker.joinable()) worker.join();
        }
    }
};

// -------------------- HANDLE ONE CLIENT --------------------

void handleClient(int client, const string& clientIp) {
    auto startTime = chrono::steady_clock::now();

    auto finishLog = [&](const string& host,
                         const string& action,
                         long long bytes) {
        auto endTime = chrono::steady_clock::now();

        double latency = chrono::duration<double, milli>(
            endTime - startTime).count();

        logRequest(clientIp, host, action, bytes, latency);
    };

    char buffer[16384];

    ssize_t n = recv(client, buffer, sizeof(buffer), 0);

    if (n <= 0) return;

    string request(buffer, static_cast<size_t>(n));

    size_t lineEnd = request.find("\r\n");
    size_t headersEnd = request.find("\r\n\r\n");

    if (lineEnd == string::npos || headersEnd == string::npos) {
        const string response =
            "HTTP/1.1 400 Bad Request\r\n"
            "Connection: close\r\nContent-Length: 0\r\n\r\n";

        sendAll(client, response.c_str(), response.size());
        finishLog("-", "ERROR", response.size());
        return;
    }

    istringstream firstLine(request.substr(0, lineEnd));

    string method, target, version;
    firstLine >> method >> target >> version;

    if (method.empty() || target.empty() || version.empty()) {
        finishLog("-", "ERROR", 0);
        return;
    }

    // HTTPS CONNECT
    if (method == "CONNECT") {
        size_t colon = target.rfind(':');

        if (colon == string::npos || colon == 0 ||
            colon == target.size() - 1) {
            const string response =
                "HTTP/1.1 400 Bad Request\r\n"
                "Connection: close\r\nContent-Length: 0\r\n\r\n";

            sendAll(client, response.c_str(), response.size());
            finishLog(target, "ERROR", response.size());
            return;
        }

        string host = target.substr(0, colon);
        string port = target.substr(colon + 1);

        if (isBlocked(host)) {
            const string response =
                "HTTP/1.1 403 Forbidden\r\n"
                "Connection: close\r\nContent-Length: 0\r\n\r\n";

            sendAll(client, response.c_str(), response.size());

            cout << "BLOCKED HTTPS: " << host << '\n';
            finishLog(host, "BLOCKED", response.size());
            return;
        }

        int destination = connectToHost(host, port);

        if (destination < 0) {
            const string response =
                "HTTP/1.1 502 Bad Gateway\r\n"
                "Connection: close\r\nContent-Length: 0\r\n\r\n";

            sendAll(client, response.c_str(), response.size());
            finishLog(host, "ERROR", response.size());
            return;
        }

        const string response =
            "HTTP/1.1 200 Connection Established\r\n\r\n";

        if (sendAll(client, response.c_str(), response.size())) {
            cout << "HTTPS tunnel established: " << host
                 << ':' << port << '\n';

            long long tunnelBytes = relay(client, destination);

            finishLog(host, "ALLOWED", tunnelBytes + response.size());
        } else {
            finishLog(host, "ERROR", response.size());
        }

        close(destination);
        return;
    }

    // HTTP proxy requests
    const string prefix = "http://";

    if (target.compare(0, prefix.size(), prefix) != 0) {
        const string response =
            "HTTP/1.1 400 Bad Request\r\n"
            "Connection: close\r\nContent-Length: 0\r\n\r\n";

        sendAll(client, response.c_str(), response.size());
        finishLog(target, "ERROR", response.size());
        return;
    }

    string address = target.substr(prefix.size());
    size_t pathStart = address.find_first_of("/?");

    string authority = address.substr(0, pathStart);
    string path = (pathStart == string::npos) ? "/" : address.substr(pathStart);

    if (!path.empty() && path[0] == '?') path = "/" + path;

    string host = authority;
    string port = "80";

    size_t colon = authority.rfind(':');

    if (colon != string::npos) {
        host = authority.substr(0, colon);
        port = authority.substr(colon + 1);
    }

    if (host.empty() || port.empty()) {
        finishLog("-", "ERROR", 0);
        return;
    }

    if (isBlocked(host)) {
        cout << "BLOCKED HTTP: " << host << '\n';

        sendBlockedResponse(client);
        finishLog(host, "BLOCKED", 0);
        return;
    }

    int destination = connectToHost(host, port);

    if (destination < 0) {
        const string response =
            "HTTP/1.1 502 Bad Gateway\r\n"
            "Connection: close\r\nContent-Length: 0\r\n\r\n";

        sendAll(client, response.c_str(), response.size());
        finishLog(host, "ERROR", response.size());
        return;
    }

    string headers = request.substr(
        lineEnd + 2, headersEnd - (lineEnd + 2));

    istringstream headerStream(headers);
    string header;
    string cleanHeaders;

    while (getline(headerStream, header)) {
        if (!header.empty() && header.back() == '\r') {
            header.pop_back();
        }

        string lower = header;

        transform(lower.begin(), lower.end(), lower.begin(),
                  [](unsigned char c) {
                      return static_cast<char>(tolower(c));
                  });

        if (lower.rfind("proxy-connection:", 0) == 0 ||
            lower.rfind("connection:", 0) == 0) {
            continue;
        }

        cleanHeaders += header + "\r\n";
    }

    string forwarded = method + " " + path + " " + version + "\r\n"
                     + cleanHeaders
                     + "Connection: close\r\n\r\n";

    size_t bodyStart = headersEnd + 4;

    if (bodyStart < request.size()) {
        forwarded += request.substr(bodyStart);
    }

    long long bytesTransferred = 0;

    if (sendAll(destination, forwarded.c_str(), forwarded.size())) {
        bytesTransferred += static_cast<long long>(forwarded.size());

        char responseBuffer[8192];

        while (true) {
            ssize_t received = recv(destination, responseBuffer,
                                    sizeof(responseBuffer), 0);

            if (received <= 0) break;

            if (!sendAll(client, responseBuffer,
                         static_cast<size_t>(received))) {
                break;
            }

            bytesTransferred += received;
        }

        finishLog(host, "ALLOWED", bytesTransferred);
    } else {
        finishLog(host, "ERROR", bytesTransferred);
    }

    close(destination);
}

// -------------------- MAIN --------------------

int main() {
    loadBlocklist();
    initDatabase();

    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);

    if (serverSocket < 0) {
        cerr << "Socket creation failed\n";
        if (db) sqlite3_close(db);
        return 1;
    }

    int reuse = 1;

    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR,
               &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    if (bind(serverSocket, reinterpret_cast<sockaddr*>(&address),
             sizeof(address)) < 0) {
        cerr << "Bind failed\n";
        close(serverSocket);
        if (db) sqlite3_close(db);
        return 1;
    }

    if (listen(serverSocket, 20) < 0) {
        cerr << "Listen failed\n";
        close(serverSocket);
        if (db) sqlite3_close(db);
        return 1;
    }

    cout << "Secure Web Gateway listening on port 8080...\n";

    ThreadPool pool(4);

    while (true) {
        sockaddr_in clientAddress{};
        socklen_t clientLength = sizeof(clientAddress);

        int client = accept(
            serverSocket,
            reinterpret_cast<sockaddr*>(&clientAddress),
            &clientLength
        );

        if (client < 0) {
            cerr << "Accept failed\n";
            continue;
        }

        char ipBuffer[INET_ADDRSTRLEN] = {};

        inet_ntop(AF_INET, &clientAddress.sin_addr,
                  ipBuffer, sizeof(ipBuffer));

        string clientIp = ipBuffer;

        pool.enqueue([client, clientIp]() {
            handleClient(client, clientIp);
            close(client);
        });
    }

    close(serverSocket);

    if (db) sqlite3_close(db);

    return 0;
}
