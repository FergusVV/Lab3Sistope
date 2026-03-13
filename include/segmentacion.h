#ifndef SEGMENTACION_H
#define SEGMENTACION_H

#include <stdint.h>

typedef struct {
    uint64_t base;
    uint64_t limit;
} segment_entry;

typedef struct {
    segment_entry *segments;
    int num_segments;
} segment_table;

/* Crear tabla de segmentos para un thread */
segment_table *seg_table_create(int num_segments, const uint64_t *limits,
                                int thread_id);

/* Traducir direccion virtual: retorna PA o -1 si segfault */
int64_t translate_segment(const segment_table *table, int seg_id,
                          uint64_t offset);

/* Liberar tabla */
void seg_table_free(segment_table *table);

#endif /* SEGMENTACION_H */
