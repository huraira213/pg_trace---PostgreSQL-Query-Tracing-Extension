# pg_trace - PostgreSQL Query Tracing Extension

A PostgreSQL extension for tracing and analyzing query execution times with persistent storage.

## Overview

pg_trace helps you monitor query performance by automatically capturing query text, execution duration, and timestamps. It provides both in-memory traces and persistent table storage for long-term analysis.

## Features

- **Automatic query interception** using PostgreSQL executor hooks
- **Per-session query tracing** with microsecond precision
- **Persistent storage** in PostgreSQL tables for long-term analysis
- **Aggregated statistics** (total, average, min, max duration)
- **SQL views** for easy access to trace data, slow queries, and statistics
- **Configuration via GUC variables**
- **Simple start/stop tracing interface** or continuous auto-trace mode

## Requirements

- PostgreSQL 16 or later
- GCC and Make
- PostgreSQL development headers

## Installation

```bash
git clone git@github.com:huraira213/pg_trace---PostgreSQL-Query-Tracing-Extension.git
cd pg_trace
make
sudo make install
```

## Usage

### Enable the Extension

```sql
CREATE EXTENSION pg_trace;
```

### Basic Tracing (Manual Mode)

```sql
-- Start tracing
SELECT pg_trace_start();

-- Run your queries
SELECT count(*) FROM pg_tables;
SELECT generate_series(1, 100);

-- Stop tracing
SELECT pg_trace_stop();

-- View traced queries (in-memory)
SELECT * FROM pg_trace_queries;

-- Flush to persistent table
SELECT pg_trace_flush();
```

### Persistent Storage

After flushing traces to the table:

```sql
-- View all traces
SELECT query_text, duration_us, duration_us/1000.0 AS duration_ms
FROM pg_trace_log
ORDER BY start_time DESC;

-- View slow queries (> 1ms)
SELECT * FROM pg_trace_slow_queries;

-- View query statistics
SELECT * FROM pg_trace_query_stats;

-- View hourly statistics
SELECT * FROM pg_trace_hourly_stats;
```

### View Statistics

```sql
-- Get summary with formatted durations
SELECT * FROM pg_trace_summary;
```

**Example output:**
```
 total_queries | total_duration_us | avg_duration_us | max_duration_us | min_duration_us | total_duration_formatted | avg_duration_formatted 
---------------+-------------------+-----------------+-----------------+-----------------+--------------------------+------------------------
             3 |               541 |             180 |             433 |              19 | 541 us                   | 180 us
```

## Function Reference

| Function | Returns | Description |
|----------|---------|-------------|
| `pg_trace_start()` | boolean | Start tracing session |
| `pg_trace_stop()` | interval | Stop tracing, return total duration |
| `pg_trace_get_queries()` | SETOF record | Get in-memory traced queries |
| `pg_trace_stats()` | record | Get session statistics |
| `pg_trace_reset_stats()` | boolean | Reset session statistics |
| `pg_trace_clear()` | boolean | Clear in-memory traces |
| `pg_trace_flush()` | integer | Flush in-memory traces to table |
| `pg_trace_cleanup_old(integer)` | integer | Delete traces older than N days |

## Views Reference

| View | Description |
|------|-------------|
| `pg_trace_queries` | In-memory traced queries |
| `pg_trace_summary` | Formatted session statistics |
| `pg_trace_log_recent` | Last 100 persisted traces |
| `pg_trace_slow_queries` | Slow queries (> 1ms) |
| `pg_trace_query_stats` | Query pattern statistics |
| `pg_trace_hourly_stats` | Hourly aggregated statistics |

## Tables

| Table | Description |
|-------|-------------|
| `pg_trace_log` | Main trace storage table |
| `pg_trace_stats_hourly` | Hourly aggregated statistics |

## Configuration

### GUC Variables

```sql
-- Enable/disable tracing
SET pg_trace.enabled = on;  -- default: on

-- Log only queries slower than N milliseconds (-1 = log all)
SET pg_trace.log_min_duration_ms = 10;  -- log queries > 10ms

-- Trace INSERT/UPDATE/DELETE in addition to SELECT
SET pg_trace.trace_non_select = on;  -- default: off

-- Check current settings
SHOW pg_trace.enabled;
SHOW pg_trace.log_min_duration_ms;
```

## Important Notes

### Session Isolation

Tracing state is per-session. All tracing commands must run in the same database session:

**Correct:**
```sql
-- All commands in one session
SELECT pg_trace_start();
SELECT 1+1;
SELECT pg_trace_stop();
SELECT * FROM pg_trace_queries;
SELECT pg_trace_flush();
```

**Incorrect:**
```bash
# Each psql -c is a separate session - won't work!
psql -c "SELECT pg_trace_start();"
psql -c "SELECT 1+1;"
psql -c "SELECT pg_trace_stop();"
```

### Query Types

By default, only `SELECT` queries are traced. Enable `pg_trace.trace_non_select` to trace INSERT/UPDATE/DELETE.

## Cleanup

```sql
-- Clear in-memory traces
SELECT pg_trace_clear();

-- Reset session statistics
SELECT pg_trace_reset_stats();

-- Delete traces older than 30 days
SELECT pg_trace_cleanup_old(30);

-- Check table size
SELECT * FROM pg_trace_table_size();
```

## Example: Performance Analysis

```sql
-- Start tracing
SELECT pg_trace_start();

-- Run your workload
SELECT count(*) FROM orders;
SELECT * FROM products WHERE price > 100;
SELECT u.name, sum(o.total) 
FROM users u 
JOIN orders o ON u.id = o.user_id 
GROUP BY u.name;

-- Stop and flush
SELECT pg_trace_stop();
SELECT pg_trace_flush();

-- Analyze performance
SELECT 
    LEFT(query_text, 50) AS query_pattern,
    duration_us / 1000.0 AS duration_ms,
    CASE 
        WHEN duration_us > 10000 THEN 'SLOW'
        WHEN duration_us > 1000 THEN 'MODERATE'
        ELSE 'FAST'
    END AS performance
FROM pg_trace_log
ORDER BY duration_us DESC;
```

## License

See LICENSE file.

## Contributing

Contributions are welcome! Please feel free to submit issues and pull requests.
