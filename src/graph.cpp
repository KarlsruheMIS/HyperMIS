#include "graph.h"
#include "fast_set.h"

#include <assert.h>
#include <iostream>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define MIN_ALLOC 8

// static inline void skip_comments(FILE *f)
// {
//     int c = fgetc_unlocked(f);
//     while (c == 'c')
//     {
//         while (c != '\n')
//             c = fgetc_unlocked(f);
//         c = fgetc_unlocked(f);
//     }
//     ungetc(c, f);
// }

// static inline void skip_line(FILE *f)
// {
//     int c = fgetc_unlocked(f);
//     while (c != '\n')
//         c = fgetc_unlocked(f);
// }

// static inline void parse_unsigned_int(FILE *f, int *v)
// {
//     int c = fgetc_unlocked(f);
//     while ((c < '0' || c > '9') && c != '\n' && c != EOF)
//         c = fgetc_unlocked(f);

//     *v = -1;
//     if (c == '\n')
//     {
//         ungetc(c, f);
//         return;
//     }

//     *v = 0;
//     while (c >= '0' && c <= '9')
//     {
//         *v = (*v * 10) + (c - '0');
//         c = fgetc_unlocked(f);
//     }
//     ungetc(c, f);
// }
static inline void parse_id(char *Data, size_t *p, long long *v)
{
    while (Data[*p] < '0' || Data[*p] > '9')
        (*p)++;

    *v = 0;
    while (Data[*p] >= '0' && Data[*p] <= '9')
        *v = (*v) * 10 + Data[(*p)++] - '0';
}

static inline void parse_id(char *Data, size_t *p, NodeID *v)
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

graph *graph_init(NodeID n, NodeID m)
{
    graph *g = (graph *)malloc(sizeof(graph));
    g->n = n;
    g->m = m;

    g->V = (NodeID *)malloc(sizeof(NodeID) * (n + 1));
    g->E = (NodeID *)malloc(sizeof(NodeID) * m);

    return g;
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

    long long n, m, t, w = 0;
    parse_id(Data, &p, &n);
    parse_id(Data, &p, &m);
    while (Data[p] == ' ')
        p++;
    if (Data[p] >= '0' && Data[p] <= '9')
        parse_id(Data, &p, &t);

    skip_line(Data, &p);

    int edge_weights = t == 11;

    if (t != 0 && t !=11) 
    {
        fprintf(stderr, "Vertex weights are not supported.");
        exit(1);
    }
    if (n >= INT_MAX)
    {
        fprintf(stderr, "Number of vertices must be less than %d, got %lld\n", INT_MAX, n);
        exit(1);
    }

    graph *g = graph_init((NodeID)n, (NodeID)m * 2);

    NodeID e = 0;
    for (NodeID u = 0; u < n; u++)
    {
        NodeID v = n;
        while (p < size && Data[p] == '%')
            skip_line(Data, &p);

        if (edge_weights)
            parse_id(Data, &p, &w); // skip the weights
            // parse_id(Data, &p, W + u);

        parse_id(Data, &p, &v);
        g->V[u] = e;

        int j = 0;
        while (!(Data[p] == '\n' || Data[p]==EOF))
        {
            v--;
            g->E[e++] = v;

            parse_id(Data, &p, &v);
        }
    }
    assert(e==m*2);
    g->V[n] = e;

    return g;
}

void graph_free(graph *g)
{
    free(g->V);
    free(g->E);

    free(g);
}