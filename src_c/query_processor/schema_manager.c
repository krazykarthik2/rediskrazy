#include "schema_manager.h"
#include <stdlib.h>
#include <string.h>

static SchemaContext context = {0};

void schema_init(void) {
    context.databases = NULL;
    context.num_databases = 0;
    context.current_db_idx = -1;
}

int create_database(const char *name) {
    for (int i = 0; i < context.num_databases; i++) {
        if (strcmp(context.databases[i].name, name) == 0) return -1; // Already exists
    }
    context.databases = realloc(context.databases, sizeof(Database) * (context.num_databases + 1));
    context.databases[context.num_databases].name = strdup(name);
    context.databases[context.num_databases].tables = NULL;
    context.databases[context.num_databases].num_tables = 0;
    context.num_databases++;
    return 0;
}

int create_table(const char *db_name, const char *table_name, Column *cols, int num_cols) {
    Database *db = get_database(db_name);
    if (!db) return -1;
    
    for (int i = 0; i < db->num_tables; i++) {
        if (strcmp(db->tables[i].name, table_name) == 0) return -1; // Already exists
    }
    
    db->tables = realloc(db->tables, sizeof(Table) * (db->num_tables + 1));
    Table *t = &db->tables[db->num_tables];
    t->name = strdup(table_name);
    t->num_columns = num_cols;
    t->columns = malloc(sizeof(Column) * num_cols);
    for (int i = 0; i < num_cols; i++) {
        t->columns[i].name = strdup(cols[i].name);
        t->columns[i].type = cols[i].type;
        t->columns[i].not_null = cols[i].not_null;
    }
    db->num_tables++;
    return 0;
}

Database* get_database(const char *name) {
    for (int i = 0; i < context.num_databases; i++) {
        if (strcmp(context.databases[i].name, name) == 0) return &context.databases[i];
    }
    return NULL;
}

Table* get_table(Database *db, const char *name) {
    if (!db) return NULL;
    for (int i = 0; i < db->num_tables; i++) {
        if (strcmp(db->tables[i].name, name) == 0) return &db->tables[i];
    }
    return NULL;
}

void schema_free(void) {
    for (int i = 0; i < context.num_databases; i++) {
        Database *db = &context.databases[i];
        for (int j = 0; j < db->num_tables; j++) {
            Table *t = &db->tables[j];
            for (int k = 0; k < t->num_columns; k++) {
                free(t->columns[k].name);
            }
            free(t->columns);
            free(t->name);
        }
        free(db->tables);
        free(db->name);
    }
    free(context.databases);
    context.databases = NULL;
    context.num_databases = 0;
}
