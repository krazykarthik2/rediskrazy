#ifndef SCHEMA_MANAGER_H
#define SCHEMA_MANAGER_H

#include "../backend/sds.h"

typedef enum {
    TYPE_STRING,
    TYPE_INT,
    // Add more as needed
} DataType;

typedef struct {
    char *name;
    DataType type;
    int not_null;
    // Potentially primary key, etc.
} Column;

typedef struct {
    char *name;
    Column *columns;
    int num_columns;
} Table;

typedef struct {
    char *name;
    Table *tables;
    int num_tables;
} Database;

typedef struct {
    Database *databases;
    int num_databases;
    int current_db_idx;
} SchemaContext;

// Global or context-based schema management
void schema_init(void);
int create_database(const char *name);
int create_table(const char *db_name, const char *table_name, Column *cols, int num_cols);
Database* get_database(const char *name);
Table* get_table(Database *db, const char *name);
void schema_free(void);

#endif
