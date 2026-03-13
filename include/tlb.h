#ifndef TLB_H
#define TLB_H

#include <stdint.h>

typedef struct {
    uint64_t vpn;
    uint64_t frame_number;
    int valid;
} tlb_entry;

typedef struct {
    tlb_entry *entries;
    int size;        /* Tamano maximo */
    int next_index;  /* Proximo slot FIFO para insercion */
    int count;       /* Entradas validas actuales */
} tlb_t;

/* Crear TLB con tamano dado */
tlb_t *tlb_init(int size);

/* Buscar VPN en TLB. Retorna frame_number o -1 si miss */
int64_t tlb_lookup(const tlb_t *tlb, uint64_t vpn);

/* Insertar entrada en TLB con politica FIFO */
void tlb_insert(tlb_t *tlb, uint64_t vpn, uint64_t frame);

/* Invalidar entrada por VPN */
void tlb_invalidate_entry(tlb_t *tlb, uint64_t vpn);

/* Invalidar entradas que apunten a un frame dado */
void tlb_invalidate_by_frame(tlb_t *tlb, uint64_t frame);

/* Liberar TLB */
void tlb_free(tlb_t *tlb);

#endif /* TLB_H */
