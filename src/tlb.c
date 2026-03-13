#include <stdlib.h>
#include <string.h>
#include "tlb.h"

/* Crear TLB con capacidad 'size'. Todas las entradas inician invalidas.
   Usa arreglo circular con politica FIFO para reemplazo. */
tlb_t *tlb_init(int size)
{
    tlb_t *tlb = malloc(sizeof(tlb_t));
    if (!tlb) return NULL;

    tlb->size = size;
    tlb->next_index = 0;
    tlb->count = 0;
    tlb->entries = calloc(size, sizeof(tlb_entry));
    if (!tlb->entries) { free(tlb); return NULL; }

    /* Todas las entradas invalidas */
    for (int i = 0; i < size; i++)
        tlb->entries[i].valid = 0;

    return tlb;
}

/* Buscar VPN en la TLB. Retorna frame_number si hit, -1 si miss.
   Busqueda lineal sobre todas las entradas validas. */
int64_t tlb_lookup(const tlb_t *tlb, uint64_t vpn)
{
    for (int i = 0; i < tlb->size; i++) {
        if (tlb->entries[i].valid && tlb->entries[i].vpn == vpn)
            return (int64_t)tlb->entries[i].frame_number;
    }
    return -1;
}

/* Insertar entrada (vpn, frame) en TLB con politica FIFO.
   Si la TLB esta llena, reemplaza la entrada mas antigua. */
void tlb_insert(tlb_t *tlb, uint64_t vpn, uint64_t frame)
{
    /* Politica FIFO: insertar en next_index, avanzar circularmente */
    tlb->entries[tlb->next_index].vpn = vpn;
    tlb->entries[tlb->next_index].frame_number = frame;
    tlb->entries[tlb->next_index].valid = 1;

    if (tlb->count < tlb->size)
        tlb->count++;

    tlb->next_index = (tlb->next_index + 1) % tlb->size;
}

/* Invalidar entrada de TLB por VPN (busqueda lineal) */
void tlb_invalidate_entry(tlb_t *tlb, uint64_t vpn)
{
    for (int i = 0; i < tlb->size; i++) {
        if (tlb->entries[i].valid && tlb->entries[i].vpn == vpn) {
            tlb->entries[i].valid = 0;
            tlb->count--;
            return;
        }
    }
}

/* Invalidar todas las entradas que referencian un frame dado.
   Se usa durante eviction para invalidacion cruzada de TLBs. */
void tlb_invalidate_by_frame(tlb_t *tlb, uint64_t frame)
{
    for (int i = 0; i < tlb->size; i++) {
        if (tlb->entries[i].valid && tlb->entries[i].frame_number == frame) {
            tlb->entries[i].valid = 0;
            if (tlb->count > 0) tlb->count--;
        }
    }
}

/* Liberar memoria de la TLB */
void tlb_free(tlb_t *tlb)
{
    if (tlb) {
        free(tlb->entries);
        free(tlb);
    }
}
