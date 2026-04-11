#include "graph.h"
#include "fast_set.h"

#include <assert.h>
#include <iostream>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

static inline void parse_id(char *Data, size_t *p, long long *v)
{
    while (Data[*p] < '0' || Data[*p] > '9')
        (*p)++;

    *v = 0;
    while (Data[*p] >= '0' && Data[*p] <= '9')
        *v = (*v) * 10 + Data[(*p)++] - '0';
}

static inline void skip_line(char *Data, size_t *p)
{
    while (Data[*p] != '\n')
        (*p)++;
    (*p)++;
}

static inline int graph_compare(const void *a, const void *b)
{
    return (*(int *)a - *(int *)b);
}

graph *graph_parse(FILE *f)
{
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *Data = (char *)malloc(size);
    size_t red = fread(Data, 1, size, f);
    size_t p = 0;

    while (Data[p] == '%')
        skip_line(Data, &p);

    long long n, m, t = 0;
    parse_id(Data, &p, &n);
    parse_id(Data, &p, &m);
    while (Data[p] == ' ')
        p++;
    if (Data[p] >= '0' && Data[p] <= '9')
        parse_id(Data, &p, &t);

    skip_line(Data, &p);

    int vertex_weights = t >= 10,
        edge_weights = (t == 1 || t == 11);

    if (n >= INT_MAX)
    {
        fprintf(stderr, "Number of vertices must be less than %d, got %lld\n", INT_MAX, n);
        exit(1);
    }

    long long *V = (long long *)malloc(sizeof(long long) * (n + 1));
    NodeID *E = (NodeID *)malloc(sizeof(NodeID) * (m * 2));

    NodeID ei = 0;
    long long dummy_w;

    for (NodeID u = 0; u < n; u++)
    {
        V[u] = ei;

        while (p < size && Data[p] == '%')
            skip_line(Data, &p);

        if (vertex_weights)
            parse_id(Data, &p, &dummy_w);

        while (ei < m * 2)
        {
            while (Data[p] == ' ')
                p++;
            if (Data[p] == '\n' || Data[p] == EOF)
                break;

            long long e;
            parse_id(Data, &p, &e);

            if (e > n || e <= 0)
            {
                fprintf(stderr, "Edge endpoint out of bounds, {%d, %lld}\n", u + 1, e);
                exit(1);
            }

            E[ei++] = e - 1;

            if (edge_weights)
                parse_id(Data, &p, &e);
        }
        p++;

        qsort(E + V[u], ei - V[u], sizeof(NodeID), graph_compare);
    }
    V[n] = ei;

    free(Data);

    graph *g = (graph*)malloc(sizeof(graph));
    *g = (graph){.n = n, .m = m * 2, .V = V, .E = E};

    return g;
}

void graph_free(graph *g)
{
    if (g == NULL)
        return;

    free(g->V);
    free(g->E);

    free(g);
}