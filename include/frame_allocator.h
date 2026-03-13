#ifndef FRAME_ALLOCATOR_H
#define FRAME_ALLOCATOR_H

#include <stdint.h>
#include <pthread.h>
#include "tlb.h"

/* Forward declarations */
typedef struct page_table page_table;

typedef struct {
    int thread_id;
    uint64_t vpn;
    int frame;
} fifo_entry;

typedef struct {
    int *bitmap;           /* 0=libre, 1=ocupado */
    int num_frames;
    int free_count;
    /* Cola FIFO para eviction */
    fifo_entry *fifo_queue;
    int fifo_head;
    int fifo_tail;
    int fifo_count;
    int fifo_capacity;
    /* Mutex para modo SAFE */
    pthread_mutex_t mutex;
    /* Contador global de evictions */
    uint64_t evictions;
    uint64_t dirty_evictions;  /* BONUS: evictions de paginas dirty */
    /* Referencias globales para invalidacion cruzada */
    tlb_t **all_tlbs;
    page_table **all_page_tables;
    int num_threads;
    int unsafe;
} frame_allocator;

/* Crear frame allocator */
frame_allocator *fa_create(int num_frames, int unsafe);

/* Configurar referencias globales para eviction */
void fa_set_globals(frame_allocator *fa, tlb_t **all_tlbs,
                    page_table **all_page_tables, int num_threads);

/* Asignar un frame. Si no hay libres, evictar con FIFO.
   thread_id y vpn son del solicitante (para registrar en FIFO). */
int fa_allocate(frame_allocator *fa, int thread_id, uint64_t vpn);

/* Liberar frame allocator */
void fa_free(frame_allocator *fa);

#endif /* FRAME_ALLOCATOR_H */
