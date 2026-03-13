#ifndef WORKLOADS_H
#define WORKLOADS_H

#include <stdint.h>

/* Generar direccion virtual para segmentacion.
   Retorna seg_id y offset via punteros. */
void gen_va_seg_uniform(unsigned int *seed, int num_segments,
                        uint64_t max_limit, int *seg_id, uint64_t *offset);

void gen_va_seg_8020(unsigned int *seed, int num_segments,
                     uint64_t max_limit, int *seg_id, uint64_t *offset);

/* Generar direccion virtual para paginacion.
   Retorna vpn y offset via punteros. */
void gen_va_page_uniform(unsigned int *seed, int num_pages,
                         int page_size, int *vpn, int *offset);

void gen_va_page_8020(unsigned int *seed, int num_pages,
                      int page_size, int *vpn, int *offset);

#endif /* WORKLOADS_H */
