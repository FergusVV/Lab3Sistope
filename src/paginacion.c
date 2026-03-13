#include <stdlib.h>
#include <string.h>
#include "paginacion.h"

/* Crear tabla de paginas con num_pages entradas.
   Todas las paginas inician como invalidas (valid=0). */
page_table *page_table_create(int num_pages)
{
    page_table *pt = malloc(sizeof(page_table));
    if (!pt) return NULL;

    pt->num_pages = num_pages;
    pt->entries = calloc(num_pages, sizeof(page_table_entry));
    if (!pt->entries) { free(pt); return NULL; }

    /* Todas invalidas al inicio */
    for (int i = 0; i < num_pages; i++) {
        pt->entries[i].valid = 0;
        pt->entries[i].frame_number = 0;
    }

    return pt;
}

/* Liberar memoria de la tabla de paginas */
void page_table_free(page_table *pt)
{
    if (pt) {
        free(pt->entries);
        free(pt);
    }
}
