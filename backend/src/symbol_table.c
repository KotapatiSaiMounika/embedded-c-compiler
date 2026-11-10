
#include "symbol_table.h"

#include <stdlib.h>
#include <string.h>

static char *copy_string(const char *str)
{
    if (str == NULL) return NULL;

    size_t len = strlen(str) + 1;
    char *copy = malloc(len);

    if (copy != NULL)
        memcpy(copy, str, len);

    return copy;
}

void symbol_table_init(SymbolTable *table)
{
    if (table == NULL) return;

    table->symbols = NULL;
    table->count = 0;
    table->capacity = 0;
    table->current_scope = 0;
}

void symbol_table_free(SymbolTable *table)
{
    if (table == NULL) return;

    for (size_t i = 0; i < table->count; i++) {
        free(table->symbols[i].name);
        free(table->symbols[i].data_type);
    }

    free(table->symbols);
    symbol_table_init(table);
}

int symbol_table_enter_scope(SymbolTable *table)
{
    if (table == NULL) return 0;

    table->current_scope++;
    return 1;
}

int symbol_table_exit_scope(SymbolTable *table)
{
    if (table == NULL || table->current_scope == 0)
        return 0;

    size_t write_index = 0;

    for (size_t i = 0; i < table->count; i++) {
        if (table->symbols[i].scope_level == table->current_scope) {
            free(table->symbols[i].name);
            free(table->symbols[i].data_type);
        } else {
            if (write_index != i)
                table->symbols[write_index] = table->symbols[i];

            write_index++;
        }
    }

    table->count = write_index;
    table->current_scope--;

    return 1;
}

int symbol_table_exists_in_current_scope(
    const SymbolTable *table,
    const char *name)
{
    if (table == NULL || name == NULL)
        return 0;

    for (size_t i = 0; i < table->count; i++) {
        if (table->symbols[i].scope_level == table->current_scope &&
            strcmp(table->symbols[i].name, name) == 0) {
            return 1;
        }
    }

    return 0;
}

int symbol_table_insert(
    SymbolTable *table,
    const char *name,
    const char *data_type)
{
    if (table == NULL || name == NULL || data_type == NULL)
        return 0;

    if (symbol_table_exists_in_current_scope(table, name))
        return 0;

    if (table->count == table->capacity) {
        size_t new_capacity = table->capacity == 0
                                  ? 8
                                  : table->capacity * 2;

        Symbol *new_symbols = realloc(
            table->symbols,
            new_capacity * sizeof(Symbol)
        );

        if (new_symbols == NULL)
            return 0;

        table->symbols = new_symbols;
        table->capacity = new_capacity;
    }

    char *name_copy = copy_string(name);
    char *type_copy = copy_string(data_type);

    if (name_copy == NULL || type_copy == NULL) {
        free(name_copy);
        free(type_copy);
        return 0;
    }

    Symbol symbol;
    symbol.name = name_copy;
    symbol.data_type = type_copy;
    symbol.scope_level = table->current_scope;

    table->symbols[table->count++] = symbol;

    return 1;
}

const Symbol *symbol_table_lookup(
    const SymbolTable *table,
    const char *name)
{
    if (table == NULL || name == NULL)
        return NULL;

    for (size_t i = table->count; i > 0; i--) {
        const Symbol *symbol = &table->symbols[i - 1];

        if (strcmp(symbol->name, name) == 0)
            return symbol;
    }

    return NULL;
}
