#include <stdlib.h>
#include <string.h>
#include "segmentacion.h"

/* Crear tabla de segmentos para un thread.
   Asigna bases secuenciales separadas por thread_id para evitar
   solapamiento en memoria fisica entre threads. */
segment_table *seg_table_create(int num_segments, const uint64_t *limits,
                                int thread_id)
{
    segment_table *table = malloc(sizeof(segment_table));
    if (!table) return NULL;

    table->num_segments = num_segments;
    table->segments = malloc(sizeof(segment_entry) * num_segments);
    if (!table->segments) { free(table); return NULL; }

    /* Calcular suma total de limites para offset por thread */
    uint64_t total_limit_sum = 0;
    for (int i = 0; i < num_segments; i++)
        total_limit_sum += limits[i];

    /* Base inicial de este thread: thread_id * total_limit_sum */
    uint64_t base_offset = (uint64_t)thread_id * total_limit_sum;

    /* Asignar bases secuencialmente */
    uint64_t current_base = base_offset;
    for (int i = 0; i < num_segments; i++) {
        table->segments[i].base = current_base;
        table->segments[i].limit = limits[i];
        current_base += limits[i];
    }

    return table;
}

/* Traducir direccion virtual (seg_id, offset) a direccion fisica.
   Retorna PA = base + offset, o -1 si offset >= limit (segfault). */
int64_t translate_segment(const segment_table *table, int seg_id,
                          uint64_t offset)
{
    if (seg_id < 0 || seg_id >= table->num_segments)
        return -1; /* segfault */

    if (offset >= table->segments[seg_id].limit)
        return -1; /* segfault: offset fuera de limite */

    return (int64_t)(table->segments[seg_id].base + offset);
}

/* Liberar memoria de la tabla de segmentos */
void seg_table_free(segment_table *table)
{
    if (table) {
        free(table->segments);
        free(table);
    }
}
