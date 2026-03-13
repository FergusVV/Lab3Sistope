Laboratorio 3 - Simulador Concurrente de Memoria Virtual


Descripción

Simulador concurrente de memoria virtual en C con pthreads. Implementa dos modos excluyentes:

- Segmentación: Traducción VA→PA con tabla de segmentos (base+offset), detección de segfaults cuando el offset excede el limite del segmento.
- Paginación: Traducción VA→PA con tabla de paginas, TLB con politica FIFO, frame allocator con eviction FIFO global e invalidación cruzada de TLBs.

Soporta concurrencia con modo safe (mutex en frame allocator) y unsafe (sin protección, para observar race conditions).

Requisitos

- GCC con soporte C11
- pthreads (POSIX threads)
- Linux/Ubuntu

sudo apt install build-essential

Compilación

make          # Compila el simulador
make clean    # Limpia binarios y archivos generados

Ejecución

bash
Ejemplo segmentación
./simulator --mode seg --threads 2 --ops-per-thread 5000 --stats

Ejemplo paginación con TLB
./simulator --mode page --threads 4 --ops-per-thread 10000 \
  --pages 128 --frames 64 --tlb-size 32 --stats

Modo unsafe (sin locks)
./simulator --mode page --threads 8 --ops-per-thread 5000 \
  --frames 8 --unsafe --stats
```

Flags disponibles

| Flag | Descripción | Default |
|------|-------------|---------|
| --mode | seg o page (obligatorio) | - |
| --threads | Numero de threads | 1 |
| --ops-per-thread | Operaciones por thread | 1000 |
| --workload | uniform o 80-20 | uniform |
| --seed | Semilla para reproducibilidad | 42 |
| --unsafe | Deshabilitar locks | false |
| --stats | Mostrar reporte en consola | false |
| --segments | Numero de segmentos (seg) | 4 |
| --seg-limits | Limites CSV (seg) | 4096,... |
| --pages | Paginas virtuales (page) | 64 |
| --frames | Frames físicos (page) | 32 |
| --page-size | Tamaño de pagina (page) | 4096 |
| --tlb-size | Entradas TLB, 0=off (page) | 16 |
| --tlb-policy | Política TLB (page) | fifo |
| --evict-policy | Política eviction (page) | fifo |

Tests

bash
make test

Ejecuta 3 suites de tests:
- test_segmentacion: PA correcta, segfaults, reproducibilidad
- test_paginacion: Page faults, TLB hit/miss, eviction FIFO, dirty pages
- test_concurrencia: Reproducibilidad SAFE, ejecución UNSAFE multi-thread

Reproducción de Experimentos

bash
make reproduce

Ejecuta los 3 experimentos obligatorios y genera JSONs en out/:

1. Exp 1 - Segmentación con segfaults controlados (limites asimétricos)
2. Exp 2a/2b - Impacto de TLB (sin TLB vs con TLB de 32 entradas)
3. Exp 3a/3b - Thrashing (1 thread vs 8 threads con solo 8 frames)

Analisis de Resultados

Experimento 1: Segmentación con Segfaults Controlados

Configuracion: --segments 4 --seg-limits 1024,2048,4096,8192 --seed 100 --ops-per-thread 10000

| Metrica | Valor |
|---------|-------|
| translations_ok | 4721 |
| segfaults | 5279 |
| translations_ok + segfaults | 10000 (== ops_per_thread) |

Con limites [1024, 2048, 4096, 8192] y offsets generados en [0, 8191], los segmentos con limites pequeños generan mas segfaults. Segmento 0 (limit=1024) produce segfault ~87.5% de las veces (offset >= 1024 en rango [0,8191]). Segmento 3 (limit=8192) nunca produce segfault. La invariante translations_ok + segfaults == ops_per_thread se cumple siempre.

Experimento 2: Impacto del TLB (Paginación)

Configuración: --pages 128 --frames 64 --workload 80-20 --ops-per-thread 50000 --seed 200

| Metrica | Sin TLB (size=0) | Con TLB (size=32) |
|---------|-------------------|-------------------|
| tlb_hits | 0 | 29156 |
| tlb_misses | 50000 | 20844 |
| hit_rate | 0.00% | 58.31% |
| page_faults | 9252 | 9252 |
| evictions | 9188 | 9188 |
| avg_translation_time_ns | 1009135 | 1009374 |
| throughput (ops/seg) | 991 | 991 |

Sin TLB, cada acceso es un miss y debe consultar la tabla de paginas. Con TLB de 32 entradas y workload 80-20 (localidad temporal), el 58.31% de los accesos son TLB hits. Los page faults y evictions son idénticos porque la secuencia de VPNs es la misma (misma semilla). El throughput es similar porque esta dominado por el costo de los page faults (nanosleep 1-5ms), que es ordenes de magnitud mayor que el ahorro del TLB hit. En un sistema real, el TLB evita accesos a memoria (no a disco), por lo que su beneficio seria mas notorio.

Experimento 3: Thrashing con Múltiples Threads

Configuración: --pages 64 --frames 8 --workload uniform --ops-per-thread 10000 --seed 300

| Métrica | 1 thread | 8 threads |
|---------|----------|-----------|
| tlb_hits | 1245 | 1377 |
| tlb_misses | 8755 | 78623 |
| hit_rate | 12.45% | 1.72% |
| page_faults | 8755 | 78623 |
| evictions | 8747 | 78615 |
| throughput (ops/seg) | 242 | 644 |
| throughput por thread | 242 | ~80 |
| tiempo total (seg) | 41.30 | 124.26 |

Con 64 paginas virtuales y solo 8 frames, el thrashing es inevitable. Con 1 thread, el hit rate es 12.45% (solo 8 de 64 paginas caben en memoria). Con 8 threads compitiendo por los mismos 8 frames, las evictions se multiplican x9 y el hit rate cae a 1.72% porque cada thread invalida constantemente las paginas de los demás. El throughput absoluto con 8 threads (644 ops/seg) es mayor porque hay 80000 operaciones totales, pero el throughput por thread (~80 ops/seg) es 3x peor que con 1 thread (242 ops/seg), evidenciando la degradación por thrashing y contención en el mutex del frame allocator.

Estructura del Proyecto

lab3sistope/
├── include/
│   ├── simulator.h
│   ├── segmentacion.h
│   ├── paginacion.h
│   ├── tlb.h
│   ├── frame_allocator.h
│   └── workloads.h
├── src/
│   ├── simulator.c         # Main, CLI, threads, output
│   ├── segmentacion.c      # Tabla de segmentos, traducción
│   ├── paginacion.c        # Tabla de paginas
│   ├── tlb.c               # TLB con FIFO
│   ├── frame_allocator.c   # Frame allocator + eviction FIFO
│   └── workloads.c         # Generadores uniform y 80-20
├── tests/
│   ├── test_segmentacion.c
│   ├── test_paginacion.c
│   └── test_concurrencia.c
├── out/                     # JSONs de salida (generados)
├── Makefile
└── README.md

Decisiones de Diseño

- Un solo mutex (mutex_frame_alloc): Las tablas de paginas y TLBs son privadas por thread. Solo el frame allocator es compartido.
- Evictions global: La cola FIFO de eviction es global. Un thread puede evictar la pagina de otro thread.
- Invalidación cruzada: Al evictar un frame, se invalidan TODAS las TLBs que referencian ese frame.
- Reproducibilidad: rand_r con semilla base_seed + thread_id garantiza las mismas direcciones por thread.
