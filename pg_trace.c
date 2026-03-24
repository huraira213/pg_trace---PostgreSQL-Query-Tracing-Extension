/*
 * pg_trace - PostgreSQL Query Tracing Extension
 * Version 5.0 - Persistent Table-Based Tracing
 *
 * This extension traces query execution times and stores them in tables
 * for persistence and analysis.
 */

#include "postgres.h"
#include "fmgr.h"
#include "executor/executor.h"
#include "utils/timestamp.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "funcapi.h"
#include "commands/extension.h"
#include "miscadmin.h"
#include "utils/guc.h"
#include "executor/spi.h"

PG_MODULE_MAGIC;

/* ==================== Configuration ==================== */

static bool pg_trace_enabled = true;
static bool pg_trace_auto_trace = false;
static int pg_trace_log_min_duration_ms = -1;  /* -1 = log all */
static bool pg_trace_trace_non_select = false;
static bool pg_trace_use_table_storage = false;  /* Manual flag for table storage */

/* ==================== Structures ==================== */

/* Query trace structure */
typedef struct QueryTrace
{
    char       *query_text;
    TimestampTz start_time;
    TimestampTz end_time;
    TimestampTz duration;
    Oid userid;
    Oid dbid;
    struct QueryTrace *next;
} QueryTrace;

/* Statistics structure */
typedef struct
{
    uint64 total_queries;
    uint64 total_duration;
    TimestampTz max_duration;
    TimestampTz min_duration;
    bool min_set;
} TraceStats;

/* ==================== Global State ==================== */

/* Session tracing state */
static TimestampTz trace_start_time = 0;
static bool tracing_active = false;

/* Query trace list */
static QueryTrace *trace_list = NULL;
static QueryTrace *current_trace = NULL;
static MemoryContext trace_memctx = NULL;

/* Statistics */
static TraceStats trace_stats = {0, 0, 0, 0, false};

/* Hook storage */
static ExecutorStart_hook_type prev_ExecutorStart = NULL;
static ExecutorEnd_hook_type prev_ExecutorEnd = NULL;

/* ==================== Function Declarations ==================== */

void _PG_init(void);
static void pg_trace_executor_start(QueryDesc *queryDesc, int eflags);
static void pg_trace_executor_end(QueryDesc *queryDesc);
static void add_trace_to_list(QueryTrace *trace);
static void ensure_tracing_active(void);
static MemoryContext get_trace_memory_context(void);
static void define_custom_variables(void);
static void pg_trace_store_to_table(QueryTrace *trace);
static bool should_trace_query(QueryDesc *queryDesc, TimestampTz duration);

/* SQL Functions */
PG_FUNCTION_INFO_V1(pg_trace_start);
PG_FUNCTION_INFO_V1(pg_trace_stop);
PG_FUNCTION_INFO_V1(pg_trace_get_queries);
PG_FUNCTION_INFO_V1(pg_trace_clear);
PG_FUNCTION_INFO_V1(pg_trace_stats);
PG_FUNCTION_INFO_V1(pg_trace_reset_stats);
PG_FUNCTION_INFO_V1(pg_trace_enable_auto);
PG_FUNCTION_INFO_V1(pg_trace_disable_auto);
PG_FUNCTION_INFO_V1(pg_trace_flush);
PG_FUNCTION_INFO_V1(pg_trace_cleanup_old);

/* ==================== GUC Variables ==================== */

static void
define_custom_variables(void)
{
    DefineCustomBoolVariable(
        "pg_trace.enabled",
        "Enable or disable pg_trace extension.",
        NULL,
        &pg_trace_enabled,
        true,
        PGC_USERSET,
        GUC_NOT_IN_SAMPLE,
        NULL, NULL, NULL);

    DefineCustomBoolVariable(
        "pg_trace.auto_trace",
        "Automatically trace all queries without explicit start/stop.",
        NULL,
        &pg_trace_auto_trace,
        false,
        PGC_USERSET,
        GUC_NOT_IN_SAMPLE,
        NULL, NULL, NULL);

    DefineCustomIntVariable(
        "pg_trace.log_min_duration_ms",
        "Log queries taking longer than this many milliseconds. -1 = log all.",
        NULL,
        &pg_trace_log_min_duration_ms,
        -1,
        -1,
        INT_MAX,
        PGC_USERSET,
        GUC_NOT_IN_SAMPLE,
        NULL, NULL, NULL);

    DefineCustomBoolVariable(
        "pg_trace.trace_non_select",
        "Trace INSERT, UPDATE, DELETE statements in addition to SELECT.",
        NULL,
        &pg_trace_trace_non_select,
        false,
        PGC_USERSET,
        GUC_NOT_IN_SAMPLE,
        NULL, NULL, NULL);
}

/* ==================== Memory Management ==================== */

static MemoryContext
get_trace_memory_context(void)
{
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

/* ==================== Helper Functions ==================== */

static bool
should_trace_query(QueryDesc *queryDesc, TimestampTz duration)
{
    /* Check if extension is enabled */
    if (!pg_trace_enabled)
        return false;

    /* Check minimum duration filter */
    if (pg_trace_log_min_duration_ms >= 0)
    {
        TimestampTz threshold = pg_trace_log_min_duration_ms * 1000;  /* convert to microseconds */
        if (duration < threshold)
            return false;
    }

    /* Check query type */
    if (!pg_trace_trace_non_select && queryDesc->operation != CMD_SELECT)
        return false;

    return true;
}

static void
pg_trace_store_to_table(QueryTrace *trace)
{
    int ret;
    char *query;
    char *escaped_query;

    /* Escape single quotes in query text */
    escaped_query = (char *) palloc(strlen(trace->query_text) * 2 + 1);
    strcpy(escaped_query, trace->query_text);
    for (char *p = escaped_query; *p; p++)
    {
        if (*p == '\'')
        {
            memmove(p + 1, p, strlen(p) + 1);
            *p++ = '\'';
        }
    }

    /* Build INSERT query - convert microseconds to timestamptz */
    query = psprintf(
        "INSERT INTO pg_trace_log (query_text, duration_us, start_time, end_time, userid, dbid, application_name) "
        "VALUES ($spi$%s$spi$, " INT64_FORMAT ", to_timestamp(%f), to_timestamp(%f), %u, %u, current_setting('application_name', true))",
        escaped_query,
        trace->duration,
        trace->start_time / 1000000.0,
        trace->end_time / 1000000.0,
        trace->userid,
        trace->dbid);

    /* Execute via SPI */
    SPI_connect();
    ret = SPI_exec(query, 0);
    if (ret != SPI_OK_INSERT)
        ereport(WARNING, (errmsg("pg_trace: failed to store trace")));
    SPI_finish();

    pfree(escaped_query);
    pfree(query);
}

/* ==================== Module Initialization ==================== */

void
_PG_init(void)
{
    define_custom_variables();

    /* Install hooks */
    prev_ExecutorStart = ExecutorStart_hook;
    ExecutorStart_hook = pg_trace_executor_start;

    prev_ExecutorEnd = ExecutorEnd_hook;
    ExecutorEnd_hook = pg_trace_executor_end;
}

/* ==================== Executor Hooks ==================== */

static void
pg_trace_executor_start(QueryDesc *queryDesc, int eflags)
{
    /* Check if we should trace this query */
    bool should_trace = false;

    if (pg_trace_enabled && queryDesc->sourceText)
    {
        /* Trace if auto-trace is enabled OR manual tracing is active */
        if (pg_trace_auto_trace || tracing_active)
        {
            if (queryDesc->operation == CMD_SELECT || pg_trace_trace_non_select)
            {
                should_trace = true;
            }
        }
    }

    if (should_trace)
    {
        MemoryContext oldctx;

        oldctx = MemoryContextSwitchTo(get_trace_memory_context());

        current_trace = (QueryTrace *)palloc0(sizeof(QueryTrace));
        current_trace->query_text = pstrdup(queryDesc->sourceText);
        current_trace->start_time = GetCurrentTimestamp();
        current_trace->userid = GetUserId();
        current_trace->dbid = MyDatabaseId;
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

    /* Complete trace if we have one */
    if (current_trace && current_trace->query_text &&
        strcmp(current_trace->query_text, queryDesc->sourceText) == 0)
    {
        current_trace->end_time = GetCurrentTimestamp();

        /* Calculate duration */
        if (current_trace->end_time >= current_trace->start_time)
            current_trace->duration = current_trace->end_time - current_trace->start_time;
        else
        {
            ereport(WARNING,
                    (errmsg("system clock moved backwards during query trace")));
            current_trace->duration = 0;
        }

        /* Check if we should store this trace */
        if (should_trace_query(queryDesc, current_trace->duration))
        {
            /* Update statistics */
            trace_stats.total_queries++;
            trace_stats.total_duration += current_trace->duration;

            if (current_trace->duration > trace_stats.max_duration)
                trace_stats.max_duration = current_trace->duration;

            if (!trace_stats.min_set || current_trace->duration < trace_stats.min_duration)
            {
                trace_stats.min_duration = current_trace->duration;
                trace_stats.min_set = true;
            }

            /* Add to list for later retrieval */
            add_trace_to_list(current_trace);
            current_trace = NULL;
        }
        else
        {
            /* Don't trace this query - free memory */
            if (current_trace->query_text)
                pfree(current_trace->query_text);
            pfree(current_trace);
            current_trace = NULL;
        }
    }
}

static void
add_trace_to_list(QueryTrace *trace)
{
    trace->next = trace_list;
    trace_list = trace;
}

/* ==================== SQL Functions ==================== */

Datum
pg_trace_start(PG_FUNCTION_ARGS)
{
    if (tracing_active)
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("tracing already active"),
                 errhint("Call pg_trace_stop() first")));

    if (!pg_trace_enabled)
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("pg_trace is disabled"),
                 errhint("Set pg_trace.enabled = on")));

    tracing_active = true;
    trace_start_time = GetCurrentTimestamp();

    ereport(NOTICE,
            (errmsg("Trace started at: " INT64_FORMAT " microseconds",
                    (int64)trace_start_time)));

    PG_RETURN_BOOL(true);
}

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

Datum
pg_trace_get_queries(PG_FUNCTION_ARGS)
{
    FuncCallContext  *funcctx;
    MemoryContext     oldcontext;
    QueryTrace       *current;
    uint32            call_cntr;
    uint32            max_calls;
    TupleDesc         tupdesc;
    AttInMetadata    *attinmeta;

    if (SRF_IS_FIRSTCALL())
    {
        funcctx = SRF_FIRSTCALL_INIT();
        oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

        max_calls = 0;
        current = trace_list;
        while (current)
        {
            max_calls++;
            current = current->next;
        }

        funcctx->max_calls = max_calls;
        funcctx->call_cntr = 0;

        if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
            ereport(ERROR,
                    (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                     errmsg("function returning record called in context "
                            "that cannot accept type record")));

        funcctx->tuple_desc = BlessTupleDesc(tupdesc);
        funcctx->attinmeta = TupleDescGetAttInMetadata(tupdesc);

        MemoryContextSwitchTo(oldcontext);
    }

    funcctx = SRF_PERCALL_SETUP();
    call_cntr = funcctx->call_cntr;
    max_calls = funcctx->max_calls;
    attinmeta = funcctx->attinmeta;

    if (call_cntr < max_calls)
    {
        const char *values[4];
        HeapTuple tuple;
        Datum result;
        uint32 i;

        current = trace_list;
        for (i = 0; i < call_cntr; i++)
            current = current->next;

        values[0] = quote_literal_cstr(current->query_text);
        values[1] = psprintf(INT64_FORMAT, current->duration);
        values[2] = timestamptz_to_str(current->start_time);
        values[3] = timestamptz_to_str(current->end_time);

        tuple = BuildTupleFromCStrings(attinmeta, (char **)values);
        result = HeapTupleGetDatum(tuple);

        pfree((void *)values[0]);
        pfree((void *)values[1]);

        funcctx->call_cntr++;
        SRF_RETURN_NEXT(funcctx, result);
    }
    else
    {
        SRF_RETURN_DONE(funcctx);
    }
}

Datum
pg_trace_clear(PG_FUNCTION_ARGS)
{
    QueryTrace *trace;

    while (trace_list)
    {
        trace = trace_list;
        trace_list = trace->next;

        if (trace->query_text)
            pfree(trace->query_text);
        pfree(trace);
    }

    if (current_trace)
    {
        if (current_trace->query_text)
            pfree(current_trace->query_text);
        pfree(current_trace);
        current_trace = NULL;
    }

    PG_RETURN_BOOL(true);
}

Datum
pg_trace_stats(PG_FUNCTION_ARGS)
{
    TupleDesc   tupdesc;
    Datum       values[5];
    bool        nulls[5] = {false};
    int64       avg_duration = 0;

    if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
        ereport(ERROR,
                (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                 errmsg("function returning record called in context "
                        "that cannot accept type record")));

    tupdesc = BlessTupleDesc(tupdesc);

    if (trace_stats.total_queries > 0)
        avg_duration = trace_stats.total_duration / trace_stats.total_queries;

    if (!trace_stats.min_set)
        trace_stats.min_duration = 0;

    values[0] = Int64GetDatum(trace_stats.total_queries);
    values[1] = Int64GetDatum(trace_stats.total_duration);
    values[2] = Int64GetDatum(avg_duration);
    values[3] = Int64GetDatum(trace_stats.max_duration);
    values[4] = Int64GetDatum(trace_stats.min_duration);

    PG_RETURN_DATUM(HeapTupleGetDatum(heap_form_tuple(tupdesc, values, nulls)));
}

Datum
pg_trace_reset_stats(PG_FUNCTION_ARGS)
{
    trace_stats.total_queries = 0;
    trace_stats.total_duration = 0;
    trace_stats.max_duration = 0;
    trace_stats.min_duration = 0;
    trace_stats.min_set = false;

    PG_RETURN_BOOL(true);
}

Datum
pg_trace_enable_auto(PG_FUNCTION_ARGS)
{
    pg_trace_auto_trace = true;
    ereport(NOTICE, (errmsg("pg_trace auto-trace enabled")));
    PG_RETURN_BOOL(true);
}

Datum
pg_trace_disable_auto(PG_FUNCTION_ARGS)
{
    pg_trace_auto_trace = false;
    ereport(NOTICE, (errmsg("pg_trace auto-trace disabled")));
    PG_RETURN_BOOL(true);
}

Datum
pg_trace_flush(PG_FUNCTION_ARGS)
{
    /* Flush in-memory traces to table */
    QueryTrace *trace = trace_list;
    int flushed = 0;

    while (trace)
    {
        QueryTrace *next = trace->next;
        pg_trace_store_to_table(trace);
        
        if (trace->query_text)
            pfree(trace->query_text);
        pfree(trace);
        
        trace = next;
        flushed++;
    }

    trace_list = NULL;

    PG_RETURN_INT32(flushed);
}

Datum
pg_trace_cleanup_old(PG_FUNCTION_ARGS)
{
    int retention_days = PG_GETARG_INT32(0);
    int deleted = 0;

    /* Delete old traces from table */
    if (retention_days > 0)
    {
        SPI_connect();
        
        char *query = psprintf(
            "DELETE FROM pg_trace_log WHERE start_time < now() - interval '%d days'",
            retention_days);
        
        int ret = SPI_exec(query, 0);
        if (ret == SPI_OK_DELETE)
            deleted = SPI_processed;
        
        SPI_finish();
        pfree(query);
    }

    PG_RETURN_INT32(deleted);
}

static void
ensure_tracing_active(void)
{
    if (!tracing_active)
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("tracing not active"),
                 errhint("Call pg_trace_start() first")));
}
