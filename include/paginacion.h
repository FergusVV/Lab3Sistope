#ifndef PAGINACION_H
#define PAGINACION_H

#include <stdint.h>

typedef struct {
    uint64_t frame_number;
    int valid;
    int dirty;  /* BONUS: 1 si la pagina fue modificada (escritura) */
} page_table_entry;

typedef struct page_table {
    page_table_entry *entries;
    int num_pages;
} page_table;

/* Crear tabla de paginas */
page_table *page_table_create(int num_pages);

/* Liberar tabla de paginas */
void page_table_free(page_table *pt);

#endif /* PAGINACION_H */
