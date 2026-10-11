# Secure Web Gateway — Benchmark Results

## Environment
- Platform: Ubuntu 22.04 under WSL2
- Test destination: http://example.org/
- Proxy address: 127.0.0.1:8080
- Measurement: wall-clock time for concurrent HTTP requests

## Results

| Concurrent Requests | Successful Requests | Elapsed Time (s) | Throughput (requests/s) |
|---:|---:|---:|---:|
| 1  | 1/1   | 0.276 | 3.62 |
| 5  | 5/5   | 0.156 | 32.05 |
| 10 | 10/10 | 0.234 | 42.74 |
| 20 | 20/20 | 0.487 | 41.07 |

All 36 requests succeeded in this run.

## Limitations
These are end-to-end measurements against an external website.
Results include network and remote-server latency and are not a
measurement of maximum proxy capacity. Repeat runs may vary.
