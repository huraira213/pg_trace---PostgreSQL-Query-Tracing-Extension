# Makefile
MODULE_big = pg_trace
EXTENSION = pg_trace
DATA = pg_trace--3.0.sql
PG_CONFIG = pg_config

# Disable bitcode generation to avoid mkdir issues
NO_INSTALLCHECK = 1

# Explicitly specify source files
OBJS = pg_trace.o

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
