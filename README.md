# pg_trace - PostgreSQL Query Tracing Extension

Hey! This is pg_trace, a simple PostgreSQL extension I made for tracing queries. It's in early stages, so it's basic right now.

## What is pg_trace?

pg_trace helps you time how long things take in PostgreSQL. Start tracing, do stuff, stop and see the duration. Later, it'll hook into queries automatically.

**Right now: Stage 2 - Basic timing!**

## Features

- Start tracing to begin timing
- Stop tracing to get the time elapsed
- Handles clock issues safely

## How to Install

You need:
- PostgreSQL 16+
- gcc and make
- PostgreSQL dev stuff

Steps:
```bash
cd pg_trace
make
sudo make install
sudo -u postgres psql -c "CREATE EXTENSION pg_trace;"
```

## How to Use

Basic commands:
```sql
-- Start tracing
SELECT pg_trace_start();

-- Do some queries here...

-- Stop and get duration
SELECT pg_trace_stop();
```

## Testing

Try this:
```bash
sudo -u postgres psql -c "
  SELECT pg_trace_start();
  -- Wait a bit or run queries
  SELECT pg_trace_stop();
"
```

## What's Next?

- ~~Stage 1: Basic toggle~~ 
- ~~Stage 2: Add timing for queries~~ 
- Stage 3: Use PostgreSQL hooks
- Stage 4: Store data in memory
- Stage 5: Use shared memory
- Stage 6: Full tracing ready for production

## Files

- pg_trace.c - The main code
- pg_trace.control - Extension info
- pg_trace--2.0.sql - SQL stuff
- Makefile - For building

## License

Check LICENSE file.

## Contributing

Feel free to help! This is new, so ideas welcome.