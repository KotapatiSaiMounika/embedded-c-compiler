
#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include <stddef.h>

typedef struct {
    char *name;
    char *data_type;
    int scope_level;
} Symbol;

typedef struct {
    Symbol *symbols;
    size_t count;
    size_t capacity;
    int current_scope;
} SymbolTable;

void symbol_table_init(SymbolTable *table);
void symbol_table_free(SymbolTable *table);

int symbol_table_enter_scope(SymbolTable *table);
int symbol_table_exit_scope(SymbolTable *table);

int symbol_table_insert(
    SymbolTable *table,
    const char *name,
    const char *data_type
);

const Symbol *symbol_table_lookup(
    const SymbolTable *table,
    const char *name
);

int symbol_table_exists_in_current_scope(
    const SymbolTable *table,
    const char *name
);

#endif
