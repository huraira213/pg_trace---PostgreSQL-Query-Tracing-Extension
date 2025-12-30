CREATE OR REPLACE FUNCTION pg_trace_version()
RETURNS text
AS 'MODULE_PATHNAME', 'pg_trace_version'
LANGUAGE C STRICT;

CREATE OR REPLACE FUNCTION pg_trace_enable()
RETURNS boolean
AS 'MODULE_PATHNAME', 'pg_trace_enable'
LANGUAGE C STRICT;  

CREATE OR REPLACE FUNCTION pg_trace_disable()
RETURNS boolean
AS 'MODULE_PATHNAME', 'pg_trace_disable'
LANGUAGE C STRICT;

CREATE OR REPLACE FUNCTION pg_trace_status()
RETURNS boolean
AS 'MODULE_PATHNAME', 'pg_trace_status'
LANGUAGE C STRICT;

-- Convenience view
CREATE OR REPLACE VIEW pg_trace_info AS
SELECT 
    pg_trace_version() as version,
    pg_trace_status() as enabled;