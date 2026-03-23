/*
 * pg_trace - PostgreSQL Query Tracing Extension
 *
 * This extension traces query execution times using PostgreSQL hooks.
 */

#include "postgres.h"
#include "fmgr.h"
#include "executor/executor.h"
#include "utils/timestamp.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "funcapi.h"
#include "commands/extension.h"
#include "storage/ipc.h"
#include "storage/lwlock.h"
#include "miscadmin.h"

PG_MODULE_MAGIC;

/* ==================== Global State ==================== */

/* Session-level tracing state */
static TimestampTz trace_start_time = 0;
static bool tracing_active = false;

/* Hook storage */
static ExecutorStart_hook_type prev_ExecutorStart = NULL;
static ExecutorEnd_hook_type prev_ExecutorEnd = NULL;

/* Query trace structure */
typedef struct QueryTrace
{
    char       *query_text;       /* The query string */
    TimestampTz start_time;       /* Query start time */
    TimestampTz end_time;         /* Query end time */
    TimestampTz duration;         /* Duration in microseconds */
    struct QueryTrace *next;      /* Next trace in linked list */
} QueryTrace;

/* Session trace list */
static QueryTrace *trace_list = NULL;
static QueryTrace *current_trace = NULL;
static MemoryContext trace_memctx = NULL;

/* Statistics counters */
static int64 total_queries_traced = 0;
static TimestampTz total_duration_all = 0;
static TimestampTz max_duration = 0;
static TimestampTz min_duration = -1;  /* -1 means not set yet */

/* ==================== Function Declarations ==================== */

static void pg_trace_init(void);
static void pg_trace_executor_start(QueryDesc *queryDesc, int eflags);
static void pg_trace_executor_end(QueryDesc *queryDesc);
static void add_trace_to_list(QueryTrace *trace);
static void ensure_tracing_active(void);
static MemoryContext get_trace_memory_context(void);
static void pg_trace_cleanup(void);

/* SQL Functions */
PG_FUNCTION_INFO_V1(pg_trace_start);
PG_FUNCTION_INFO_V1(pg_trace_stop);
PG_FUNCTION_INFO_V1(pg_trace_get_queries);
PG_FUNCTION_INFO_V1(pg_trace_clear);
PG_FUNCTION_INFO_V1(pg_trace_stats);
PG_FUNCTION_INFO_V1(pg_trace_reset_stats);

/* ==================== Module Initialization ==================== */

void
_PG_init(void)
{
    pg_trace_init();
}

static void
pg_trace_init(void)
{
    /* Install hooks */
    prev_ExecutorStart = ExecutorStart_hook;
    ExecutorStart_hook = pg_trace_executor_start;

    prev_ExecutorEnd = ExecutorEnd_hook;
    ExecutorEnd_hook = pg_trace_executor_end;
}

/* ==================== Memory Management ==================== */

static MemoryContext
get_trace_memory_context(void)
{
    /* Create a long-lived memory context for storing traces */
    if (trace_memctx == NULL)
    {
        trace_memctx = AllocSetContextCreate(TopMemoryContext,
                                             "pg_trace context",
                                             ALLOCSET_DEFAULT_MINSIZE,
                                             ALLOCSET_DEFAULT_INITSIZE,
                                             ALLOCSET_DEFAULT_MAXSIZE);
    }
    return trace_memctx;
}

/* ==================== Cleanup Callback ==================== */

/*
 * pg_trace_cleanup - Clean up trace memory on session exit
 * Note: Currently not registered for automatic cleanup, but available for future use.
 *       Traces are stored in TopMemoryContext and will be freed on session end.
 */
static void
pg_trace_cleanup(void)
{
    QueryTrace *trace;

    /* Free all trace records */
    while (trace_list)
    {
        trace = trace_list;
        trace_list = trace->next;

        if (trace->query_text)
            pfree(trace->query_text);
        pfree(trace);
    }

    /* Reset current trace if any */
    if (current_trace)
    {
        if (current_trace->query_text)
            pfree(current_trace->query_text);
        pfree(current_trace);
        current_trace = NULL;
    }

    /* Reset statistics */
    total_queries_traced = 0;
    total_duration_all = 0;
    max_duration = 0;
    min_duration = -1;

    /* Delete memory context */
    if (trace_memctx)
    {
        MemoryContextDelete(trace_memctx);
        trace_memctx = NULL;
    }
}

/* Suppress unused warning - will be used in future versions for session cleanup */
__attribute__((unused))
static void pg_trace_cleanup_wrapper(void) { pg_trace_cleanup(); }

/* ==================== Executor Hooks ==================== */

static void
pg_trace_executor_start(QueryDesc *queryDesc, int eflags)
{
    /* Only trace if tracing is active and we have a valid query string */
    if (tracing_active && queryDesc->sourceText &&
        queryDesc->operation == CMD_SELECT)
    {
        MemoryContext oldctx;

        /* Allocate trace record in long-lived memory context */
        oldctx = MemoryContextSwitchTo(get_trace_memory_context());

        current_trace = (QueryTrace *)palloc0(sizeof(QueryTrace));
        current_trace->query_text = pstrdup(queryDesc->sourceText);
        current_trace->start_time = GetCurrentTimestamp();
        current_trace->next = NULL;

        MemoryContextSwitchTo(oldctx);
    }

    /* Call previous hook */
    if (prev_ExecutorStart)
        prev_ExecutorStart(queryDesc, eflags);
    else
        standard_ExecutorStart(queryDesc, eflags);
}

static void
pg_trace_executor_end(QueryDesc *queryDesc)
{
    /* Call previous hook first */
    if (prev_ExecutorEnd)
        prev_ExecutorEnd(queryDesc);
    else
        standard_ExecutorEnd(queryDesc);

    /* Record trace if we have an active trace */
    if (tracing_active && current_trace &&
        current_trace->query_text && queryDesc->sourceText &&
        strcmp(current_trace->query_text, queryDesc->sourceText) == 0)
    {
        current_trace->end_time = GetCurrentTimestamp();

        /* Calculate duration safely */
        if (current_trace->end_time >= current_trace->start_time)
            current_trace->duration = current_trace->end_time - current_trace->start_time;
        else
        {
            ereport(WARNING,
                    (errmsg("system clock moved backwards during query trace")));
            current_trace->duration = 0;
        }

        /* Update statistics */
        total_queries_traced++;
        total_duration_all += current_trace->duration;

        if (current_trace->duration > max_duration)
            max_duration = current_trace->duration;

        if (min_duration < 0 || current_trace->duration < min_duration)
            min_duration = current_trace->duration;

        /* Add to trace list */
        add_trace_to_list(current_trace);
        current_trace = NULL;
    }
}

static void
add_trace_to_list(QueryTrace *trace)
{
    /* Add to beginning of list (most recent first) */
    trace->next = trace_list;
    trace_list = trace;
}

/* ==================== SQL Functions ==================== */

/*
 * pg_trace_start() - Start tracing session
 */
Datum
pg_trace_start(PG_FUNCTION_ARGS)
{
    if (tracing_active)
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("tracing already active"),
                 errhint("Call pg_trace_stop() first")));

    tracing_active = true;
    trace_start_time = GetCurrentTimestamp();

    ereport(NOTICE,
            (errmsg("Trace started at: " INT64_FORMAT " microseconds since 2000-01-01",
                    (int64)trace_start_time)));

    PG_RETURN_BOOL(true);
}

/*
 * pg_trace_stop() - Stop tracing and return total duration
 */
Datum
pg_trace_stop(PG_FUNCTION_ARGS)
{
    TimestampTz stop_time;
    TimestampTz duration;
    Interval   *result;

    ensure_tracing_active();

    stop_time = GetCurrentTimestamp();

    if (stop_time >= trace_start_time)
        duration = stop_time - trace_start_time;
    else
    {
        ereport(WARNING,
                (errmsg("system clock moved backwards during trace")));
        duration = 0;
    }

    result = (Interval *)palloc(sizeof(Interval));
    result->time = duration;
    result->day = 0;
    result->month = 0;

    tracing_active = false;
    trace_start_time = 0;

    PG_RETURN_INTERVAL_P(result);
}

/*
 * pg_trace_get_queries() - Return all traced queries
 *
 * Returns a setof record with columns:
 *   query_text text,
 *   duration_us bigint,
 *   start_time timestamptz,
 *   end_time timestamptz
 */
Datum
pg_trace_get_queries(PG_FUNCTION_ARGS)
{
    FuncCallContext  *funcctx;
    MemoryContext     oldcontext;
    QueryTrace       *current;
    int               call_cntr;
    int               max_calls;
    TupleDesc         tupdesc;
    AttInMetadata    *attinmeta;

    /* Stuff done only on the first call */
    if (SRF_IS_FIRSTCALL())
    {
        /* Create a function context for cross-call persistence */
        funcctx = SRF_FIRSTCALL_INIT();

        /* Switch to memory context appropriate for multiple function calls */
        oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

        /* Count traces */
        max_calls = 0;
        current = trace_list;
        while (current)
        {
            max_calls++;
            current = current->next;
        }

        funcctx->max_calls = max_calls;
        funcctx->call_cntr = 0;

        /* Build a tuple descriptor */
        if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
            ereport(ERROR,
                    (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                     errmsg("function returning record called in context "
                            "that cannot accept type record")));

        funcctx->tuple_desc = BlessTupleDesc(tupdesc);
        funcctx->attinmeta = TupleDescGetAttInMetadata(tupdesc);

        MemoryContextSwitchTo(oldcontext);
    }

    /* Stuff done on every call */
    funcctx = SRF_PERCALL_SETUP();
    call_cntr = funcctx->call_cntr;
    max_calls = funcctx->max_calls;
    attinmeta = funcctx->attinmeta;

    if (call_cntr < max_calls)
    {
        /* Find the current trace record */
        int i;
        const char *values[4];
        HeapTuple tuple;
        Datum result;

        current = trace_list;
        for (i = 0; i < call_cntr; i++)
            current = current->next;

        /* Build values for this row */
        values[0] = quote_literal_cstr(current->query_text);
        values[1] = psprintf(INT64_FORMAT, current->duration);
        values[2] = timestamptz_to_str(current->start_time);
        values[3] = timestamptz_to_str(current->end_time);

        /* Build the tuple */
        tuple = BuildTupleFromCStrings(attinmeta, (char **)values);
        result = HeapTupleGetDatum(tuple);

        /* Free some memory */
        pfree((void *)values[0]);
        pfree((void *)values[1]);

        funcctx->call_cntr++;
        SRF_RETURN_NEXT(funcctx, result);
    }
    else
    {
        /* No more traces to return */
        SRF_RETURN_DONE(funcctx);
    }
}

/*
 * pg_trace_clear() - Clear all stored traces
 */
Datum
pg_trace_clear(PG_FUNCTION_ARGS)
{
    QueryTrace *trace;

    /* Free all trace records */
    while (trace_list)
    {
        trace = trace_list;
        trace_list = trace->next;

        if (trace->query_text)
            pfree(trace->query_text);
        pfree(trace);
    }

    /* Reset current trace if any */
    if (current_trace)
    {
        if (current_trace->query_text)
            pfree(current_trace->query_text);
        pfree(current_trace);
        current_trace = NULL;
    }

    PG_RETURN_BOOL(true);
}

/*
 * pg_trace_stats() - Return tracing statistics
 *
 * Returns:
 *   total_queries bigint,
 *   total_duration_us bigint,
 *   avg_duration_us bigint,
 *   max_duration_us bigint,
 *   min_duration_us bigint
 */
Datum
pg_trace_stats(PG_FUNCTION_ARGS)
{
    TupleDesc   tupdesc;
    Datum       values[5];
    bool        nulls[5] = {false};
    int64       avg_duration = 0;

    /* Build tuple descriptor if needed */
    if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
        ereport(ERROR,
                (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                 errmsg("function returning record called in context "
                        "that cannot accept type record")));

    tupdesc = BlessTupleDesc(tupdesc);

    /* Calculate average */
    if (total_queries_traced > 0)
        avg_duration = total_duration_all / total_queries_traced;

    /* Handle case where no queries traced yet */
    if (min_duration < 0)
        min_duration = 0;

    /* Fill values */
    values[0] = Int64GetDatum(total_queries_traced);
    values[1] = Int64GetDatum(total_duration_all);
    values[2] = Int64GetDatum(avg_duration);
    values[3] = Int64GetDatum(max_duration);
    values[4] = Int64GetDatum(min_duration);

    PG_RETURN_DATUM(HeapTupleGetDatum(heap_form_tuple(tupdesc, values, nulls)));
}

/*
 * pg_trace_reset_stats() - Reset statistics counters
 */
Datum
pg_trace_reset_stats(PG_FUNCTION_ARGS)
{
    total_queries_traced = 0;
    total_duration_all = 0;
    max_duration = 0;
    min_duration = -1;

    PG_RETURN_BOOL(true);
}

/* ==================== Cleanup ==================== */

static void
ensure_tracing_active(void)
{
    if (!tracing_active)
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("tracing not active"),
                 errhint("Call pg_trace_start() first")));
}
