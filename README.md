# pg_trace - PostgreSQL Query Tracing Extension

A PostgreSQL extension for tracing and analyzing query execution times.

## Overview

pg_trace helps you monitor query performance by automatically capturing query text, execution duration, and timestamps. It provides both detailed query traces and aggregated statistics.

## Features

- Automatic query interception using PostgreSQL executor hooks
- Per-session query tracing with microsecond precision
- Aggregated statistics (total, average, min, max duration)
- SQL views for easy access to trace data
- Simple start/stop tracing interface

## Requirements

- PostgreSQL 16 or later
- GCC and Make
- PostgreSQL development headers

## Installation

```bash
git clone <repository-url>
cd pg_trace
make
sudo make install
```

## Usage

### Enable the Extension

```sql
CREATE EXTENSION pg_trace;
```

### Basic Tracing

```sql
-- Start tracing
SELECT pg_trace_start();

-- Run your queries
SELECT count(*) FROM pg_tables;
SELECT generate_series(1, 100);

-- Stop tracing and get total duration
SELECT pg_trace_stop();
```

### View Traced Queries

```sql
-- View all traced queries
SELECT * FROM pg_trace_queries;
```

**Example output:**
```
            query_text             | duration_us |          start_time           |           end_time            
-----------------------------------+-------------+-------------------------------+-------------------------------
 'SELECT count(*) FROM pg_tables;' |         433 | 2026-03-23 22:36:24.125594+05 | 2026-03-23 22:36:24.125594+05
 'SELECT generate_series(...);'    |          89 | 2026-03-23 22:36:24.120000+05 | 2026-03-23 22:36:24.120089+05
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
| `pg_trace_get_queries()` | SETOF record | Get all traced queries |
| `pg_trace_stats()` | record | Get aggregated statistics |
| `pg_trace_reset_stats()` | boolean | Reset statistics counters |
| `pg_trace_clear()` | boolean | Clear all stored traces |

## Views Reference

| View | Description |
|------|-------------|
| `pg_trace_queries` | View all traced queries with timing |
| `pg_trace_summary` | Formatted statistics summary |

## Important Notes

### Session Isolation

Tracing state is per-session. All tracing commands must run in the same database session:

```sql
-- Correct: all commands in one session
SELECT pg_trace_start();
SELECT 1+1;
SELECT pg_trace_stop();
SELECT * FROM pg_trace_queries;
```

### Query Types

Currently only `SELECT` queries are traced.

## Cleanup

```sql
-- Clear stored queries
SELECT pg_trace_clear();

-- Reset statistics
SELECT pg_trace_reset_stats();
```

## License

See LICENSE file.


