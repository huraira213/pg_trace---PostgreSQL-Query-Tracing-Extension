#include "postgres.h"
#include "fmgr.h"
#include "utils/builtins.h"
#include <string.h>

PG_MODULE_MAGIC;

/* ---------- Module Variables ---------- */
static bool tracing_enabled = false;  

PG_FUNCTION_INFO_V1(pg_trace_version);
PG_FUNCTION_INFO_V1(pg_trace_enable);
PG_FUNCTION_INFO_V1(pg_trace_disable);
PG_FUNCTION_INFO_V1(pg_trace_status);


// Function for Version information
Datum pg_trace_version(PG_FUNCTION_ARGS)
{
    const char *version = "pg_trace version 1.0.0 - Stage 1 : Foundation\n";
    text *result = cstring_to_text(version);
    PG_RETURN_TEXT_P(result);
}

// Enable/Disable Functions
Datum pg_trace_enable(PG_FUNCTION_ARGS)
{
    tracing_enabled = true;
    
    ereport(INFO, (errmsg("pg_trace: tracing enabled.")));

    PG_RETURN_BOOL(true);
}

Datum pg_trace_disable(PG_FUNCTION_ARGS)
{
    tracing_enabled = false;
    ereport(INFO, (errmsg("pg_trace: tracing disabled.")));
    PG_RETURN_BOOL(false);
}

Datum pg_trace_status(PG_FUNCTION_ARGS)
{
    PG_RETURN_BOOL(tracing_enabled);
}