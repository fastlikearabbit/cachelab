#include "cachelab.h"
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <ctype.h>

/* constants */
#define ADDR_SIZE 64
static size_t num_lines;
static size_t set_bits;
static size_t block_bits;
static char *trace_path;

/* begin linked list implementation */
typedef struct list_node list_node;
struct list_node {
    size_t tag;
    list_node *next;
};

typedef struct {
    size_t *head;
    list_node *tail;
} list;

/* last node is dummy */
list *list_new() {
    list_node *dummy = malloc(sizeof(list_node));
    list *L = malloc(sizeof(list));
    L->head = dummy;
    L->tail = dummy;
    return L;
}

void list_append(list *L, size_t tag) {
    list_node *new_dummy = malloc(sizeof(list_node));
    L->tail->tag = tag;
    L->tail->next = new_dummy;
    L->tail = new_dummy;
}

void list_prepend(list *L, size_t tag) {
    list_node *new_node = malloc(sizeof(list_node));
    new_node->tag = tag;
    new_node->next = L->head;
    L->head = new_node;
}

size_t list_delete_head(list *L) {
    list_node *tmp = L->head;
    size_t head_tag = tmp->tag;
    L->head = L->head->next;
    free(tmp);
    return head_tag;
}

void list_delete_by_tag(list *L, size_t tag) {
    if (L->head->tag == tag) {
        list_node *tmp = L->head;
        L->head = L->head->next;
        free(tmp);
        return;
    }

    for (list_node *p = L->head; p != L->tail; p = p->next) {
        if (p->next != NULL && p->next->tag == tag) {
            list_node *tmp = p->next;
            p->next = p->next->next;
            free(tmp);
        }
    }
}

void list_free(list *L) {
    for (list_node *p = L->head; p != L->tail;) {
        list_node *tmp = p;
        p = p->next;
        free(tmp);
    }
    free(L);
}

void list_print(list *L) {
    for (list_node *p = L->head; p != L->tail; p = p->next) {
        printf("%lli --> ", p->tag);
        if (p->next == L->tail) printf("NULL");
    }
    printf("\n");
}

typedef list *list_t;
/* end linked list implementation */

/* type declarations */
typedef struct {
    int valid;      /* determines whether the line contains valid data */
    size_t tag;  /* uniquely identifies the lines within the set    */
} cache_line;

typedef struct {
    cache_line *cache_lines;  /* num_lines per cache set */
    list_t lru_list;      /* contains least recently used with lru at head */
} cache_set;
// lru_list can be represented as a linked list of tags


/* begin print utilities  */
void print_summary(int hit_count, int miss_count, int eviction_count) {
    printf("hits:%i misses:%i evictions:%i\n", hit_count, miss_count, eviction_count);
}

void print_stats() {
    printf("Number of set bits: %i\n", set_bits);
    printf("Number of lines per set: %i\n", num_lines);
    printf("Number of block bits: %i\n", block_bits);
    printf("Trace file: %s\n", trace_path);
}

void print_usage() {
    printf("Usage: ./csim [-hv] -s <s> -E <E> -b <b> -t <tracefile>\n");
}
/* end print utilities */

/* begin address parsing */
size_t get_set_index(size_t address) {
    size_t mask = ~(0xFFFFFFFF << set_bits);
    return (address >> block_bits) & mask;
}

size_t get_tag(size_t address) {
    return address >> (set_bits + block_bits);
}
/* end address parsing */

void parse_input(int argc, char **argv) {
    int ch;

    while ((ch = getopt(argc, argv, "hvs:E:b:t:")) != -1) {
        switch (ch) {
            case 'h':
                print_usage();
                break;
            case 'v':
                break;
            case 's':
                set_bits = atoi(optarg);
                break;
            case 'E':
                num_lines = atoi(optarg);
                break;
            case 'b':
                block_bits = atoi(optarg);
                break;
            case 't':
                trace_path = optarg;
                break;
            default:
                printf("Incorrect format\n");
                print_usage();
                break;
        }
    }
}

/* begin cache utilities */
cache_set *cache_init() {
    size_t num_sets = pow(2, set_bits);
    cache_set *C = malloc(sizeof(cache_set) * num_sets);
    if (C == NULL) {
        printf("malloc failed \n");
        exit(1);
    }
    for (size_t i = 0; i < num_sets; i++) {
        C[i].cache_lines = malloc(num_lines * sizeof(cache_line));
        if (C[i].cache_lines == NULL) {
            printf("malloc failed\n");
            exit(1);
        }
        C[i].lru_list = list_new();
    }
    return C;
}

void cache_free(cache_set *C) {
    size_t num_sets = pow(2, set_bits);
    for (size_t i = 0; i < num_sets; i++) {
        free(C[i].cache_lines);
        list_free(C[i].lru_list);
    }
    free(C);
}

int cache_load(cache_set *C, size_t address) {
    size_t set_index = get_set_index(address);
    size_t tag = get_tag(address);
    cache_line *lines = C[set_index].cache_lines;
    list_t lru = C[set_index].lru_list;

    for (int i = 0; i < num_lines; i++) {
        if (lines[i].valid && lines[i].tag == tag) {
            list_delete_by_tag(lru, tag);
            list_append(lru, tag);
            return 1;  /* hit */
        }
    }
    /* miss + possible eviction, try first-fit */
    for (size_t i = 0; i < num_lines; i++) {
        if (!lines[i].valid) {
            lines[i].valid = 1;
            lines[i].tag = tag;
            list_append(lru, tag);
            return 0;  /* miss */
        }
        if (i == num_lines - 1) { 
            size_t v_tag = list_delete_head(lru);
            for (size_t j = 0; j < num_lines; j++) {
                if (lines[j].tag == v_tag) {
                    lines[j].tag = tag;
                    list_append(lru, tag);
                    return -1;  /* eviction */
                }
            }
        }
    }
    return 0;
}

int cache_store(cache_set *C, size_t address) {
    return cache_load(C, address);
}

/* end cache utilities */

void parse_file(int *hit_count, int *miss_count, int *eviction_count) {
    FILE *trace = fopen(trace_path, "r");
    char line[256];
    cache_set *C = cache_init();

    while (fgets(line, sizeof line, trace) != NULL) {
        if (line[0] != ' ') continue;
        size_t addr = strtol(line + 3, NULL, 16);
        printf("address %lli\n", addr);
        switch (line[1]) {
            case 'L':    /* load data */
            case 'S': {  /* store data */
                int res = cache_load(C, addr);
                if (res < 0) {
                    (*miss_count)++;
                    (*eviction_count)++;
                } else if (res == 0) {
                    (*miss_count)++;
                } else {
                    (*hit_count)++;
                }
                break;
            }
            case 'M':  {  /* modify: load + store data */
                int res = cache_load(C, addr);
                if (res > 0) {
                    (*hit_count) += 2;
                } else if (res < 0) {
                    (*miss_count)++;
                    (*eviction_count)++;
                    (*hit_count)++;
                } else {
                    (*miss_count)++;
                    (*hit_count)++;
                }
                break;
            }
            default:
                continue;
        }

    }
    fclose(trace);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Provide necessary arguments\n");
        print_usage();
        return 1;
    }
    parse_input(argc, argv);
    int hit_count= 0, miss_count = 0, eviction_count = 0;
	  parse_file(&hit_count, &miss_count, &eviction_count); 
    printSummary(hit_count, miss_count, eviction_count);

    return 0;
}
