-- pg_trace extension - SQL functions and tables
-- Version 5.0 - Persistent Table-Based Tracing

-- ==================== Configuration Tables ====================

-- Main trace log table
CREATE TABLE IF NOT EXISTS pg_trace_log (
    id bigserial PRIMARY KEY,
    query_text text NOT NULL,
    duration_us bigint NOT NULL,
    start_time timestamptz NOT NULL,
    end_time timestamptz NOT NULL,
    userid oid NOT NULL,
    dbid oid NOT NULL,
    application_name text,
    client_addr inet,
    created_at timestamptz DEFAULT now()
);

-- Indexes for common queries
CREATE INDEX IF NOT EXISTS idx_pg_trace_log_duration ON pg_trace_log(duration_us DESC);
CREATE INDEX IF NOT EXISTS idx_pg_trace_log_start_time ON pg_trace_log(start_time DESC);
CREATE INDEX IF NOT EXISTS idx_pg_trace_log_userid ON pg_trace_log(userid);
CREATE INDEX IF NOT EXISTS idx_pg_trace_log_dbid ON pg_trace_log(dbid);

-- Hourly statistics table
CREATE TABLE IF NOT EXISTS pg_trace_stats_hourly (
    hour timestamptz PRIMARY KEY,
    total_queries bigint NOT NULL,
    total_duration_us bigint NOT NULL,
    avg_duration_us bigint NOT NULL,
    max_duration_us bigint NOT NULL,
    min_duration_us bigint NOT NULL,
    updated_at timestamptz DEFAULT now()
);

-- ==================== Functions ====================

-- Start tracing session (manual mode)
CREATE OR REPLACE FUNCTION pg_trace_start()
RETURNS boolean
AS 'MODULE_PATHNAME', 'pg_trace_start'
LANGUAGE C STRICT;

-- Stop tracing and return total duration
CREATE OR REPLACE FUNCTION pg_trace_stop()
RETURNS interval
AS 'MODULE_PATHNAME', 'pg_trace_stop'
LANGUAGE C STRICT;

-- Get all traced queries from memory (manual mode)
CREATE OR REPLACE FUNCTION pg_trace_get_queries(
    OUT query_text text,
    OUT duration_us bigint,
    OUT start_time timestamptz,
    OUT end_time timestamptz
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pg_trace_get_queries'
LANGUAGE C;

-- Clear in-memory traces
CREATE OR REPLACE FUNCTION pg_trace_clear()
RETURNS boolean
AS 'MODULE_PATHNAME', 'pg_trace_clear'
LANGUAGE C STRICT;

-- Get session statistics
CREATE OR REPLACE FUNCTION pg_trace_stats(
    OUT total_queries bigint,
    OUT total_duration_us bigint,
    OUT avg_duration_us bigint,
    OUT max_duration_us bigint,
    OUT min_duration_us bigint
)
RETURNS record
AS 'MODULE_PATHNAME', 'pg_trace_stats'
LANGUAGE C;

-- Reset session statistics
CREATE OR REPLACE FUNCTION pg_trace_reset_stats()
RETURNS boolean
AS 'MODULE_PATHNAME', 'pg_trace_reset_stats'
LANGUAGE C STRICT;

-- Enable auto-trace mode (automatically log all queries to table)
CREATE OR REPLACE FUNCTION pg_trace_enable_auto()
RETURNS boolean
AS 'MODULE_PATHNAME', 'pg_trace_enable_auto'
LANGUAGE C STRICT;

-- Disable auto-trace mode
CREATE OR REPLACE FUNCTION pg_trace_disable_auto()
RETURNS boolean
AS 'MODULE_PATHNAME', 'pg_trace_disable_auto'
LANGUAGE C STRICT;

-- Flush in-memory traces to table
CREATE OR REPLACE FUNCTION pg_trace_flush()
RETURNS integer
AS 'MODULE_PATHNAME', 'pg_trace_flush'
LANGUAGE C STRICT;

-- Cleanup old traces (older than N days)
CREATE OR REPLACE FUNCTION pg_trace_cleanup_old(retention_days integer)
RETURNS integer
AS 'MODULE_PATHNAME', 'pg_trace_cleanup_old'
LANGUAGE C STRICT;

-- ==================== Views ====================

-- View for in-memory traces (manual mode)
CREATE OR REPLACE VIEW pg_trace_queries AS
SELECT * FROM pg_trace_get_queries();

-- View for session statistics
CREATE OR REPLACE VIEW pg_trace_summary AS
SELECT 
    total_queries,
    total_duration_us,
    avg_duration_us,
    max_duration_us,
    min_duration_us,
    CASE 
        WHEN total_duration_us > 1000000 THEN round((total_duration_us::numeric / 1000000)::numeric, 3) || ' s'
        WHEN total_duration_us > 1000 THEN round((total_duration_us::numeric / 1000)::numeric, 3) || ' ms'
        ELSE total_duration_us || ' us'
    END AS total_duration_formatted,
    CASE 
        WHEN avg_duration_us > 1000000 THEN round((avg_duration_us::numeric / 1000000)::numeric, 3) || ' s'
        WHEN avg_duration_us > 1000 THEN round((avg_duration_us::numeric / 1000)::numeric, 3) || ' ms'
        ELSE avg_duration_us || ' us'
    END AS avg_duration_formatted
FROM pg_trace_stats();

-- View for persisted traces from table
CREATE OR REPLACE VIEW pg_trace_log_recent AS
SELECT 
    id,
    query_text,
    duration_us,
    duration_us / 1000.0 AS duration_ms,
    start_time,
    end_time,
    userid,
    dbid,
    application_name,
    created_at
FROM pg_trace_log
ORDER BY start_time DESC
LIMIT 100;

-- View for slow queries (top 50 by duration)
CREATE OR REPLACE VIEW pg_trace_slow_queries AS
SELECT 
    id,
    query_text,
    duration_us,
    duration_us / 1000.0 AS duration_ms,
    start_time,
    application_name
FROM pg_trace_log
WHERE duration_us > 1000  -- More than 1ms
ORDER BY duration_us DESC
LIMIT 50;

-- View for query statistics by pattern
CREATE OR REPLACE VIEW pg_trace_query_stats AS
SELECT 
    LEFT(query_text, 100) AS query_pattern,
    count(*) AS execution_count,
    sum(duration_us) AS total_duration_us,
    avg(duration_us)::bigint AS avg_duration_us,
    max(duration_us) AS max_duration_us,
    min(duration_us) AS min_duration_us,
    max(start_time) AS last_executed
FROM pg_trace_log
GROUP BY LEFT(query_text, 100)
ORDER BY total_duration_us DESC
LIMIT 100;

-- View for hourly statistics
CREATE OR REPLACE VIEW pg_trace_hourly_stats AS
SELECT 
    date_trunc('hour', start_time) AS hour,
    count(*) AS total_queries,
    sum(duration_us) AS total_duration_us,
    avg(duration_us)::bigint AS avg_duration_us,
    max(duration_us) AS max_duration_us,
    min(duration_us) AS min_duration_us
FROM pg_trace_log
GROUP BY date_trunc('hour', start_time)
ORDER BY hour DESC
LIMIT 168;  -- Last week (168 hours)

-- ==================== Helper Functions ====================

-- Get table size
CREATE OR REPLACE FUNCTION pg_trace_table_size()
RETURNS TABLE (
    table_name text,
    row_count bigint,
    total_size text,
    index_size text,
    toast_size text
) AS $$
BEGIN
    RETURN QUERY
    SELECT 
        'pg_trace_log'::text,
        (SELECT reltuples::bigint FROM pg_class WHERE relname = 'pg_trace_log'),
        pg_size_pretty(pg_total_relation_size('pg_trace_log')),
        pg_size_pretty(pg_indexes_size('pg_trace_log')),
        pg_size_pretty(pg_total_relation_size('pg_trace_log') - 
                      pg_relation_size('pg_trace_log') - 
                      pg_indexes_size('pg_trace_log'));
END;
$$ LANGUAGE plpgsql;
