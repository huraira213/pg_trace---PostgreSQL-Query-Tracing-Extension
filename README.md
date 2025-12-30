# pg_trace - PostgreSQL Query Tracing Extension

A PostgreSQL extension for query performance tracing and analysis.

## Overview

`pg_trace` is designed to provide detailed insights into query execution times and performance metrics within PostgreSQL. This extension allows developers and database administrators to monitor and analyze query performance in real-time.

**Current Stage: Stage 1 - Foundation**  
This is the initial release with basic extension structure and toggle functionality. Future stages will add timing, hooks, and shared memory features.

## Features

- **Version Information**: Get extension version details
- **Tracing Control**: Enable/disable tracing functionality
- **Status Monitoring**: Check current tracing state
- **Convenience View**: `pg_trace_info` for quick status overview

## Installation

### Prerequisites

- PostgreSQL 16 or later
- Development tools (gcc, make)
- PostgreSQL development headers

### Build and Install

```bash
# Clone or navigate to the extension directory
cd pg_trace

# Build the extension
make

# Install the extension (requires sudo)
sudo make install

# Create the extension in your database
sudo -u postgres psql -c "CREATE EXTENSION pg_trace;"
```

## Usage

### Basic Functions

```sql
-- Get extension version
SELECT pg_trace_version();

-- Enable tracing
SELECT pg_trace_enable();

-- Check tracing status
SELECT pg_trace_status();

-- Disable tracing
SELECT pg_trace_disable();
```

### Convenience View

```sql
-- Quick overview of extension status
SELECT * FROM pg_trace_info;
```

## Testing

Run the following commands to verify the installation:

```bash
# Test all functions
sudo -u postgres psql -c "
  SELECT pg_trace_version();
  SELECT pg_trace_enable();
  SELECT pg_trace_status();
  SELECT pg_trace_disable();
  SELECT pg_trace_status();
"

# Test the view
sudo -u postgres psql -c "SELECT * FROM pg_trace_info;"
```

## Development Roadmap

- **Stage 1 (Current)**: Basic extension structure and toggle functionality ✅
- **Stage 2**: Session-based timing functions
- **Stage 3**: Introduction to PostgreSQL hooks
- **Stage 4**: Session memory storage
- **Stage 5**: Shared memory introduction
- **Stage 6**: Full production timing with hooks

## Files

- `pg_trace.c` - Main C source code
- `pg_trace.control` - Extension metadata
- `pg_trace--1.0.sql` - SQL function definitions
- `Makefile` - Build configuration

## License

See LICENSE file for details.

## Contributing

This extension is under active development. Contributions and feedback are welcome!