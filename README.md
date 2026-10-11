# Secure Web Gateway in C++

A lightweight HTTP forward proxy built in C++ on Linux/WSL. The gateway forwards HTTP requests, establishes HTTPS CONNECT tunnels, enforces hostname-based access policies, and records request activity in SQLite.

## Features

- **HTTP forwarding:** Forwards HTTP requests to destination servers.
- **HTTPS CONNECT tunneling:** Supports HTTPS tunnels without decrypting TLS traffic.
- **Domain-based filtering:** Blocks configured hostnames using a blocklist.
- **HTTP 403 responses:** Rejects blocked HTTP requests with a Forbidden response.
- **Concurrent request handling:** Processes multiple client requests concurrently.
- **SQLite logging:** Records request timestamps, client IP addresses, hostnames, actions, transferred bytes, and latency.
- **CLI statistics:** Displays request totals, allowed and blocked counts, frequently blocked domains, average latency, and recent requests.
- **Automated tests:** Tests HTTP blocking, allowed HTTP requests, HTTPS blocking, concurrent requests, and SQLite logging.

## Architecture

```text
Client (curl / browser)
         |
         v
   TCP Listener
    Port 8080
         |
         v
   Request Handler
         |
         v
   Policy Engine
     /       \
 BLOCK       ALLOW
   |            |
   v            v
HTTP 403    Destination
            Connection
                |
          HTTP forwarding
          or HTTPS CONNECT
                |
                v
          Client Response

Request metadata
        |
        v
   SQLite Database
        |
        v
    CLI Statistics
```

## Requirements

- Ubuntu Linux or WSL2
- C++ compiler supporting C++17
- SQLite3 development library
- `curl`
- Bash and standard Linux utilities

## Installation

Install the dependencies on Ubuntu:

```bash
sudo apt update
sudo apt install g++ sqlite3 libsqlite3-dev curl
```

Navigate to the project directory:

```bash
cd ~/secure-web-gateway
```

Compile the gateway:

```bash
g++ -std=c++17 -Wall -Wextra -pthread src/main.cpp -lsqlite3 -o server
```

## Running the Gateway

Start the server:

```bash
./server
```

The gateway listens on port `8080` by default. Keep the server running while sending requests from another terminal.

### Test an allowed HTTP request

```bash
curl -i -x http://127.0.0.1:8080 http://example.org/
```

### Test a blocked HTTP request

```bash
curl -i -x http://127.0.0.1:8080 http://example.com/
```

A blocked request should receive an HTTP `403 Forbidden` response when the hostname matches a configured rule.

### Test a blocked HTTPS request

```bash
curl -v --max-time 10 \
  -x http://127.0.0.1:8080 \
  https://example.com/
```

For a blocked HTTPS destination, the proxy rejects the CONNECT request with HTTP `403`. HTTPS traffic is not decrypted.

## Blocklist Configuration

Edit the configured blocklist:

```bash
nano config/blocklist.txt
```

Add one hostname or supported wildcard rule per line, following the format used by the project.

Example:

```text
example.com
*.blocked.test
```

The exact wildcard matching behavior depends on the policy implementation. Restart the gateway after changing the file if the application loads its rules only at startup.

## Automated Tests

With the gateway running in one terminal, open another terminal in the project directory and execute:

```bash
chmod +x tests/test_proxy.sh
./tests/test_proxy.sh
```

Latest recorded functional test result:

**5 passed, 0 failed**

The tests cover:

1. Blocked HTTP returns `403`.
2. Allowed HTTP succeeds.
3. Blocked HTTPS CONNECT is rejected with `403`.
4. Five concurrent HTTP requests succeed.
5. SQLite contains request records.

## SQLite Logging

Request logs are stored in `logs.db`.

Inspect recent request records:

```bash
sqlite3 logs.db \
  "SELECT timestamp, client_ip, host, action, bytes_transferred, ROUND(latency_ms, 2) FROM requests ORDER BY timestamp DESC LIMIT 10;"
```

## CLI Statistics

If the `stats` executable is present, run:

```bash
./stats
```

The statistics utility reports request totals, request counts by action, top blocked domains, average latency, and recent request records.

## Benchmark Results

The initial end-to-end benchmark was run against `http://example.org/` through the local proxy.

| Concurrent requests | Successful requests | Elapsed time | Throughput |
|---:|---:|---:|---:|
| 1 | 1/1 | 0.276 s | 3.62 requests/s |
| 5 | 5/5 | 0.156 s | 32.05 requests/s |
| 10 | 10/10 | 0.234 s | 42.74 requests/s |
| 20 | 20/20 | 0.487 s | 41.07 requests/s |

All 36 requests succeeded in the recorded run.

These measurements include external network and destination-server latency. They are preliminary end-to-end measurements, not a measure of maximum proxy capacity. Repeat runs may produce different results.

Detailed results are available in `benchmarks/results.md`.

## Current Limitations

- HTTPS traffic is tunneled without TLS decryption. Filtering is based on the destination hostname available during CONNECT.
- The benchmark uses an external HTTP destination and is affected by network conditions.
- Performance has been measured only at the listed concurrency levels.
- Authentication, advanced policy management, and a web dashboard are not claimed as implemented features.

## Project Structure

```text
secure-web-gateway/
├── src/
│   └── main.cpp
├── config/
│   └── blocklist.txt
├── tests/
│   └── test_proxy.sh
├── benchmarks/
│   └── results.md
├── logs.db
├── server
├── stats
└── README.md
```

## Future Improvements

- Improve malformed-request handling and HTTP method support.
- Add more policy and error-handling tests.
- Benchmark against a local test server to reduce external network variability.
- Add configurable timeouts and resource limits.
- Improve observability and reporting.

## License

Choose and add a license before distributing the project publicly.
