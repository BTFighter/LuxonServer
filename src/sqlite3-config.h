#ifndef SQLITE_CONFIG_H
#define SQLITE_CONFIG_H

/* --------------------------------------------------------------------
 * Core In-Memory Settings
 * -------------------------------------------------------------------- */

/* Force all temporary tables and indices to be stored in memory. */
#define SQLITE_TEMP_STORE 3

/* Disable Write-Ahead Logging (WAL) since it relies on disk I/O. */
#define SQLITE_OMIT_WAL 1

/* Disable the Backup API as we aren't saving/loading from disk. */
#define SQLITE_OMIT_BACKUP 1

/* Disable loadable extensions (requires disk I/O to load .so/.dll). */
#define SQLITE_OMIT_LOAD_EXTENSION 1


/* --------------------------------------------------------------------
 * Performance & Footprint Reductions
 * -------------------------------------------------------------------- */

/* Disable mutexing. Assumes single-threaded access to the database. 
   Change to 1 (serialized) or 2 (multi-thread) if accessing across threads, but parser references it. */
#define SQLITE_THREADSAFE 0

/* Remove all deprecated functions to save binary size. */
#define SQLITE_OMIT_DEPRECATED 1

/* Disable automatic initialization (you must call sqlite3_initialize() manually if needed, 
   though sqlite3_open handles it by default). */
#define SQLITE_OMIT_AUTOINIT 1

/* Omit memory allocation statistics tracking for a slight speed boost. */
#define SQLITE_DEFAULT_MEMSTATUS 0

/* --------------------------------------------------------------------
 * Sane Defaults for Memory 
 * -------------------------------------------------------------------- */

/* 4KB page size is a solid default for memory usage. */
#define SQLITE_DEFAULT_PAGE_SIZE 4096

/* Set default cache size to ~2MB (negative means KB). */
#define SQLITE_DEFAULT_CACHE_SIZE -2000

#endif /* SQLITE_CONFIG_H */
