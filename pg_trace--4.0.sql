-- pg_trace extension - SQL functions
-- Version 4.0 - Enhanced Per-Session Tracing

-- Start tracing session
CREATE OR REPLACE FUNCTION pg_trace_start()
RETURNS boolean
AS 'MODULE_PATHNAME', 'pg_trace_start'
LANGUAGE C STRICT;

-- Stop tracing and return total duration
CREATE OR REPLACE FUNCTION pg_trace_stop()
RETURNS interval
AS 'MODULE_PATHNAME', 'pg_trace_stop'
LANGUAGE C STRICT;

-- Get all traced queries
CREATE OR REPLACE FUNCTION pg_trace_get_queries(
    OUT query_text text,
    OUT duration_us bigint,
    OUT start_time timestamptz,
    OUT end_time timestamptz
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pg_trace_get_queries'
LANGUAGE C;

-- Clear all stored traces
CREATE OR REPLACE FUNCTION pg_trace_clear()
RETURNS boolean
AS 'MODULE_PATHNAME', 'pg_trace_clear'
LANGUAGE C STRICT;

-- Get tracing statistics
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

-- Reset statistics counters
CREATE OR REPLACE FUNCTION pg_trace_reset_stats()
RETURNS boolean
AS 'MODULE_PATHNAME', 'pg_trace_reset_stats'
LANGUAGE C STRICT;

-- View for easy access to traced queries
CREATE OR REPLACE VIEW pg_trace_queries AS
SELECT * FROM pg_trace_get_queries();

-- View for statistics with formatted durations
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
