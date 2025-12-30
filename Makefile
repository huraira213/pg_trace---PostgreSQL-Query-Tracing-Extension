# Makefile
MODULE_big = pg_trace
EXTENSION = pg_trace
DATA = pg_trace--1.0.sql
PG_CONFIG = pg_config

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
