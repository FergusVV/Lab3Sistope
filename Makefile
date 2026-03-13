CC = gcc
CFLAGS = -std=c11 -pthread -Wall -Wextra
INCLUDES = -Iinclude

SRCS = src/simulator.c src/segmentacion.c src/paginacion.c \
       src/tlb.c src/frame_allocator.c src/workloads.c
OBJS = $(SRCS:.c=.o)
TARGET = simulator

.PHONY: all run reproduce clean test

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^

run: all
	./simulator --mode seg --threads 2 --ops-per-thread 5000 --stats

reproduce: all
	mkdir -p out
	@echo "=== Experimento 1: Segmentacion con segfaults ==="
	./simulator --mode seg --threads 1 --workload uniform --ops-per-thread 10000 \
		--segments 4 --seg-limits 1024,2048,4096,8192 --seed 100 --stats
	cp out/summary.json out/exp1_summary.json
	@echo ""
	@echo "=== Experimento 2a: Paginacion SIN TLB ==="
	./simulator --mode page --threads 1 --workload 80-20 --ops-per-thread 50000 \
		--pages 128 --frames 64 --page-size 4096 --tlb-size 0 --tlb-policy fifo \
		--seed 200 --stats
	cp out/summary.json out/exp2a_summary.json
	@echo ""
	@echo "=== Experimento 2b: Paginacion CON TLB ==="
	./simulator --mode page --threads 1 --workload 80-20 --ops-per-thread 50000 \
		--pages 128 --frames 64 --page-size 4096 --tlb-size 32 --tlb-policy fifo \
		--seed 200 --stats
	cp out/summary.json out/exp2b_summary.json
	@echo ""
	@echo "=== Experimento 3a: Thrashing 1 thread ==="
	./simulator --mode page --threads 1 --workload uniform --ops-per-thread 10000 \
		--pages 64 --frames 8 --page-size 4096 --tlb-size 16 --seed 300 --stats
	cp out/summary.json out/exp3a_summary.json
	@echo ""
	@echo "=== Experimento 3b: Thrashing 8 threads ==="
	./simulator --mode page --threads 8 --workload uniform --ops-per-thread 10000 \
		--pages 64 --frames 8 --page-size 4096 --tlb-size 16 --seed 300 --stats
	cp out/summary.json out/exp3b_summary.json

test: all
	$(CC) $(CFLAGS) $(INCLUDES) -o tests/test_segmentacion tests/test_segmentacion.c \
		src/segmentacion.c src/workloads.c
	$(CC) $(CFLAGS) $(INCLUDES) -o tests/test_paginacion tests/test_paginacion.c \
		src/paginacion.c src/tlb.c src/frame_allocator.c src/workloads.c
	$(CC) $(CFLAGS) $(INCLUDES) -o tests/test_concurrencia tests/test_concurrencia.c \
		src/segmentacion.c src/paginacion.c src/tlb.c src/frame_allocator.c src/workloads.c
	@echo "--- test_segmentacion ---"
	./tests/test_segmentacion
	@echo "--- test_paginacion ---"
	./tests/test_paginacion
	@echo "--- test_concurrencia ---"
	./tests/test_concurrencia

clean:
	rm -f $(TARGET) src/*.o
	rm -f tests/test_segmentacion tests/test_paginacion tests/test_concurrencia
	rm -f out/*.json
