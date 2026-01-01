#include "postgres.h"
#include "fmgr.h"
#include "utils/timestamp.h"
#include "utils/builtins.h"  // Needed for type handling

PG_MODULE_MAGIC;

// Session state
static TimestampTz trace_start_time = 0;
static bool tracing_active = false;  // Better than checking start_time != 0

// Helper to ensure tracing is active
static void
ensure_tracing_active(void)
{
    if (!tracing_active)
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("tracing not active"),
                 errhint("Call pg_trace_start() first")));
}

// Start tracing
PG_FUNCTION_INFO_V1(pg_trace_start);
Datum
pg_trace_start(PG_FUNCTION_ARGS)
{
    // Check if already tracing
    if (tracing_active)
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("tracing already active"),
                 errhint("Call pg_trace_stop() first")));
    
    tracing_active = true;
    trace_start_time = GetCurrentTimestamp();
    
    ereport(NOTICE,
            (errmsg("Trace started at: %lld microseconds since 2000-01-01"),
             (long long)trace_start_time));
    
    // Return success (void functions are tricky in PG extensions)
    PG_RETURN_BOOL(true);
}

// Stop tracing and return interval
PG_FUNCTION_INFO_V1(pg_trace_stop);
Datum
pg_trace_stop(PG_FUNCTION_ARGS)
{
    TimestampTz stop_time;
    TimestampTz duration;
    Interval   *result;
    
    ensure_tracing_active();
    
    stop_time = GetCurrentTimestamp();
    
    // Calculate duration safely (TimestampTz is signed)
    if (stop_time >= trace_start_time) {
        duration = stop_time - trace_start_time;
    } else {
        // Clock went backwards (rare, but possible with NTP adjustments)
        ereport(WARNING,
                (errmsg("system clock moved backwards during trace")));
        duration = 0;  // Or trace_start_time - stop_time for absolute diff
    }
    
    // Allocate result in current memory context
    result = (Interval *)palloc(sizeof(Interval));
    
    // Check allocation succeeded (palloc doesn't return NULL in PG, but good practice)
    if (result == NULL)
        ereport(ERROR,
                (errcode(ERRCODE_OUT_OF_MEMORY),
                 errmsg("out of memory")));
    
    // Fill interval structure
    // Note: PostgreSQL intervals are complex!
    // time = total microseconds
    // day = days component
    // month = months component
    
    result->time = duration;   
    result->day = 0;           
    result->month = 0;         
    
    // Reset state
    tracing_active = false;
    trace_start_time = 0;
    
    PG_RETURN_INTERVAL_P(result);
}