#include "hypergraph.h"
#include "fast_set.h"

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <sys/mman.h>
#include <iostream>

#define MIN_ALLOC 8

static inline void skip_comments(FILE *f)
{
    int c = fgetc_unlocked(f);
    while (c == 'c')
    {
        while (c != '\n')
            c = fgetc_unlocked(f);
        c = fgetc_unlocked(f);
    }
    ungetc(c, f);
}

static inline void skip_line(FILE *f)
{
    int c = fgetc_unlocked(f);
    while (c != '\n')
        c = fgetc_unlocked(f);
}

static inline void parse_unsigned_int(FILE *f, int *v)
{
    int c = fgetc_unlocked(f);
    while ((c < '0' || c > '9') && c != '\n' && c != EOF)
        c = fgetc_unlocked(f);

    *v = -1;
    if (c == '\n')
    {
        ungetc(c, f);
        return;
    }

    *v = 0;
    while (c >= '0' && c <= '9')
    {
        *v = (*v * 10) + (c - '0');
        c = fgetc_unlocked(f);
    }
    ungetc(c, f);
}

static inline int hypergraph_compare(const void *a, const void *b)
{
    return (*(NodeID *)a - *(NodeID *)b);
}

static inline NodeID lower_bound(const NodeID *A, NodeID n, NodeID x)
{
    const NodeID *s = A;
    while (n > 1)
    {
        NodeID h = n / 2;
        s += (s[h - 1] < x) * h;
        n -= h;
    }
    s += (n == 1 && s[0] < x);
    return s - A;
}

void hypergraph_append_element(NodeID *l, NodeID *a, NodeID **A, NodeID v)
{
    if (*l >= *a)
    {
        *a *= 2;
        *A = (NodeID *)realloc(*A, sizeof(NodeID) * *a);
    }
    (*A)[(*l)++] = v;
}

hypergraph *hypergraph_init(NodeID n, NodeID m)
{
    hypergraph *g = (hypergraph *)malloc(sizeof(hypergraph));
    g->n = n;
    g->m = m;

    g->Vd = (NodeID *)malloc(sizeof(NodeID) * n);
    g->Va = (NodeID *)malloc(sizeof(NodeID) * n);

    g->Ed = (NodeID *)malloc(sizeof(NodeID) * m);
    g->Ea = (NodeID *)malloc(sizeof(NodeID) * m);

    if (USE_NEIGHBORHOOD_ARRAY)
    {
        g->Nd = (NodeID *)malloc(sizeof(NodeID) * n);
        g->Na = (NodeID *)malloc(sizeof(NodeID) * n);
        g->N = (NodeID **)malloc(sizeof(NodeID *) * n);
    }
    else
    {
        g->Nd = NULL;
        g->Na = NULL;
        g->N = NULL;
    }

    g->V = (NodeID **)malloc(sizeof(NodeID *) * n);
    g->E = (NodeID **)malloc(sizeof(NodeID *) * m);

    for (NodeID i = 0; i < n; i++)
    {
        g->Vd[i] = 0;
        g->Va[i] = MIN_ALLOC;
        g->V[i] = (NodeID *)malloc(sizeof(NodeID) * g->Va[i]);
        if (USE_NEIGHBORHOOD_ARRAY)
        {
            g->Nd[i] = 0;
            g->Na[i] = MIN_ALLOC;
            g->N[i] = (NodeID *)malloc(sizeof(NodeID) * g->Na[i]);
        }
    }

    for (NodeID i = 0; i < m; i++)
    {
        g->Ed[i] = 0;
        g->Ea[i] = MIN_ALLOC;
        g->E[i] = (NodeID *)malloc(sizeof(NodeID) * g->Ea[i]);
    }

    return g;
}

hypergraph *hypergraph_parse(FILE *f)
{
    skip_comments(f);

    int n, m, w, v;
    parse_unsigned_int(f, &m);
    parse_unsigned_int(f, &n);
    parse_unsigned_int(f, &w);

    hypergraph *g = hypergraph_init((NodeID)n, (NodeID)m);

    for (NodeID i = 0; i < m; i++)
    {
        skip_line(f);
        skip_comments(f);

        if (w == 11 || w == 10)
            parse_unsigned_int(f, &v); // skip the weights

        parse_unsigned_int(f, &v);
        while (v > 0)
        {
            v--;
            hypergraph_append_element(g->Vd + (NodeID)v, g->Va + (NodeID)v, g->V + (NodeID)v, i);
            hypergraph_append_element(g->Ed + i, g->Ea + i, g->E + i, (NodeID)v);

            parse_unsigned_int(f, &v);
        }
    }

    return g;
}

NodeID *hypergraph_get_neighborhood(const hypergraph *g, NodeID u, NodeID *neighborhood, NodeID &deg, fast_set &node_set)
{
    if (g->N && g->Nd)
    {
        deg = g->Nd[u];
        return g->N[u];
    }
    node_set.clear();
    node_set.add(u);
    deg = 0;
    for (NodeID i = 0; i < g->Vd[u]; i++)
    {
        NodeID e = g->V[u][i];
        for (NodeID j = 0; j < g->Ed[e]; j++)
        {
            NodeID v = g->E[e][j];
            if (node_set.add(v))
                neighborhood[deg++] = v;
        }
    }
    std::sort(neighborhood, neighborhood + deg);
    return neighborhood;
}

void hypergraph_build_neighbors(hypergraph *g, fast_set *fs)
{
    if (!USE_NEIGHBORHOOD_ARRAY)
        return;
    for (NodeID v = 0; v < g->n; v++)
    {
        NodeID k = 0;
        fs->clear();
        fs->add(v);
        for (NodeID i = 0; i < g->Vd[v]; i++)
        {
            NodeID e = g->V[v][i];
            for (NodeID j = 0; j < g->Ed[e]; j++)
            {
                NodeID u = g->E[e][j];
                if (fs->add(u))
                {
                    assert(v != u);
                    hypergraph_append_element(g->Nd + v, g->Na + v, g->N + v, u);
                }
            }
        }
    }
    for (NodeID i = 0; i < g->n; i++)
        qsort(g->N[i], g->Nd[i], sizeof(NodeID), hypergraph_compare);
}

void hypergraph_sort(hypergraph *g)
{
    for (NodeID i = 0; i < g->n; i++)
    {
        qsort(g->V[i], g->Vd[i], sizeof(NodeID), hypergraph_compare);
        if (USE_NEIGHBORHOOD_ARRAY)
            qsort(g->N[i], g->Nd[i], sizeof(NodeID), hypergraph_compare);
    }

    for (NodeID i = 0; i < g->m; i++)
        qsort(g->E[i], g->Ed[i], sizeof(NodeID), hypergraph_compare);

    for (NodeID i = 0; i < g->n; i++)
    {
        NodeID d = 0;
        for (NodeID j = 0; j < g->Vd[i]; j++)
        {
            if (j == 0 || g->V[i][j] > g->V[i][d - 1])
                g->V[i][d++] = g->V[i][j];
        }
        g->Vd[i] = d;

        if (USE_NEIGHBORHOOD_ARRAY)
        {
            d = 0;
            for (NodeID j = 0; j < g->Nd[i]; j++)
            {
                if (j == 0 || g->N[i][j] > g->N[i][d - 1])
                    g->N[i][d++] = g->N[i][j];
            }
            g->Nd[i] = d;
        }
    }
    for (NodeID i = 0; i < g->m; i++)
    {
        NodeID d = 0;
        for (NodeID j = 0; j < g->Ed[i]; j++)
        {
            if (j == 0 || g->E[i][j] > g->E[i][d - 1])
                g->E[i][d++] = g->E[i][j];
        }
        g->Ed[i] = d;
    }
}


void hypergraph_remove_element(NodeID *vec, NodeID &size, NodeID element)
{
    NodeID p = lower_bound(vec, size, element);
    assert(p < size && vec[p] == element);
    memmove(vec + p, vec + p + 1, sizeof(NodeID) * (size - p - 1));
    size--;
}

void hypergraph_remove_set(NodeID *vec, NodeID &size, fast_set *set)
{
    NodeID remaining = 0;
    for (NodeID i = 0; i < size; i++)
    {
        if (!set->get(vec[i]))
            vec[remaining++] = vec[i];
    }
    size = remaining;
}

void hypergraph_remove_vertex(hypergraph *g, NodeID u)
{
    for (NodeID i = 0; i < g->Vd[u]; i++)
    {
        NodeID e = g->V[u][i];

        hypergraph_remove_element(g->E[e], g->Ed[e], u);

        if (g->Ed[e] == 1)
            hypergraph_remove_size_one_edge(g, e);
    }

    g->Vd[u] = 0;
    g->Va[u] = MIN_ALLOC;
    g->V[u] = (NodeID *)realloc(g->V[u], sizeof(NodeID) * g->Va[u]);

    if (g->Nd)
    {
        for (NodeID i = 0; i < g->Nd[u]; i++)
        {
            NodeID v = g->N[u][i];
            assert(v != u);
            hypergraph_remove_element(g->N[v], g->Nd[v], u);
        }
        g->Nd[u] = 0;
        g->Na[u] = MIN_ALLOC;
        g->N[u] = (NodeID *)realloc(g->N[u], sizeof(NodeID) * g->Na[u]);
    }
}

void hypergraph_remove_size_one_edge(hypergraph *g, NodeID e)
{
    for (NodeID i = 0; i < g->Ed[e]; i++)
    {
        NodeID v = g->E[e][i];
        hypergraph_remove_element(g->V[v], g->Vd[v], e);
    }

    g->Ed[e] = 0;
    g->Ea[e] = MIN_ALLOC;
    g->E[e] = (NodeID *)realloc(g->E[e], sizeof(NodeID) * g->Ea[e]);
}

void hypergraph_remove_edge(hypergraph *g, NodeID e)
{
    for (NodeID i = 0; i < g->Ed[e]; i++)
    {
        NodeID v = g->E[e][i];
        hypergraph_remove_element(g->V[v], g->Vd[v], e);
    }

    g->Ed[e] = 0;
    g->Ea[e] = MIN_ALLOC;
    g->E[e] = (NodeID *)realloc(g->E[e], sizeof(NodeID) * g->Ea[e]);
}

void hypergraph_remove_edges(hypergraph *g, NodeID *E, NodeID e_size, fast_set *edges, fast_set *nodes)
{
    edges->clear();
    nodes->clear();
    for (NodeID i = 0; i < e_size; i++)
        edges->add(E[i]);

    for (NodeID i = 0; i < e_size; i++)
    {
        NodeID e = E[i];

        for (NodeID j = 0; j < g->Ed[e]; j++)
        {
            NodeID v = g->E[e][j];
            if (nodes->add(v))
                hypergraph_remove_set(g->V[v], g->Vd[v], edges);
        }

        g->Ed[e] = 0;
        g->Ea[e] = MIN_ALLOC;
        g->E[e] = (NodeID *)realloc(g->E[e], sizeof(NodeID) * g->Ea[e]);
    }
}

// remove edges and all containing vertices (include u operation)
void hypergraph_remove_neighborhood(hypergraph *g, NodeID u, fast_set *nodes, fast_set *edges)
{

    // get vertices and edges that will be deleted to skip fixing their neighborhoods
    nodes->clear();
    edges->clear();
    for (NodeID i = 0; i < g->Vd[u]; i++)
    {
        NodeID e = g->V[u][i];
        edges->add(e);
        for (NodeID j = 0; j < g->Ed[e]; j++)
        {
            NodeID v = g->E[e][j];
            nodes->add(v);
        }
    }
    if (g->Nd)
    {
        for (NodeID i = 0; i < g->Nd[u]; i++)
        {
            NodeID v = g->N[u][i];
            for (NodeID j = 0; j < g->Nd[v]; j++)
            {
                NodeID w = g->N[v][j];
                if (!nodes->get(w)) 
                    hypergraph_remove_set(g->N[w], g->Nd[w], nodes);
            }
        }
    }

    for (NodeID i = 0; i < g->Vd[u]; i++)
    {
        NodeID f = g->V[u][i];
        for (NodeID l = 0; l < g->Ed[f]; l++)
        {
            NodeID w = g->E[f][l];
            if (w == u)
                continue;

            for (NodeID j = 0; j < g->Vd[w]; j++)
            {
                NodeID e = g->V[w][j];
                if (edges->get(e))
                    continue;

                for (NodeID k = 0; k < g->Ed[e]; k++)
                {
                    NodeID v = g->E[e][k];
                    if (!nodes->get(v)) 
                        hypergraph_remove_set(g->V[v], g->Vd[v], edges);
                }

                hypergraph_remove_set(g->E[e], g->Ed[e], nodes);
                if (g->Ed[e] == 1)
                    hypergraph_remove_size_one_edge(g, e);
            }

            g->Vd[w] = 0;
            g->Va[w] = MIN_ALLOC;
            g->V[w] = (NodeID *)realloc(g->V[w], sizeof(NodeID) * g->Va[w]);
        }
        g->Ed[f] = 0;
        g->Ea[f] = MIN_ALLOC;
        g->E[f] = (NodeID *)realloc(g->E[f], sizeof(NodeID) * g->Ea[f]);
    }
    g->Vd[u] = 0;
    g->Va[u] = MIN_ALLOC;
    g->V[u] = (NodeID *)realloc(g->V[u], sizeof(NodeID) * g->Va[u]);
}

bool hypergraph_is_neighbor(hypergraph *g, NodeID u, NodeID neighbor)
{
    if (USE_NEIGHBORHOOD_ARRAY)
    {
        NodeID p = lower_bound(g->N[u], g->Nd[u], neighbor);
        return p < g->Nd[u] && g->N[u][p] == neighbor;
    }

    for (NodeID i = 0; i < g->Vd[u]; i++)
    {
        NodeID e = g->V[u][i];
        NodeID p = lower_bound(g->E[e], g->Ed[e], neighbor);
        if (p < g->Ed[e] && g->E[e][p] == neighbor)
            return true;
    }
    return false;
}

hypergraph *hypergraph_build_reduced(hypergraph *g, NodeID *map, NodeID *remap, int *reduced)
{

    NodeID red_n = 0;
    NodeID red_m = 0;
    for (NodeID v = 0; v < g->n; v++)
    {
        if (reduced[v] == 0)
        {
            map[v] = red_n;
            remap[red_n] = v;
            red_n++;
        }
    }
    for (NodeID e = 0; e < g->m; e++)
    {
        if (g->Ed[e] > 1)
            red_m++;
    }

    hypergraph *rg = hypergraph_init(red_n, red_m);
    if (red_n == 0)
        return rg;

    NodeID e = 0;
    for (NodeID i = 0; i < g->m; i++)
    {
        if (g->Ed[i] < 2)
            continue;
        for (NodeID j = 0; j < g->Ed[i]; j++)
        {
            NodeID v_original = g->E[i][j];
            if (reduced[v_original] == 0)
            {
                NodeID v = map[v_original];
                hypergraph_append_element(rg->Vd + v, rg->Va + v, rg->V + v, e);
                hypergraph_append_element(rg->Ed + e, rg->Ea + e, rg->E + e, v);
            }
        }
        e++;
    }
    assert(e <= red_m);

    if (g->Nd && rg->Nd)
    {
        for (NodeID u_new = 0; u_new < red_n; u_new++)
        {
            NodeID u = remap[u_new];
            for (NodeID j = 0; j < g->Nd[u]; j++)
            {
                NodeID v = g->N[u][j];
                if (reduced[v] == 0)
                {
                    NodeID v_new = map[v];
                    hypergraph_append_element(rg->Nd + u_new, rg->Na + u_new, rg->N + u_new, v_new);
                }
            }
        }
    }
    return rg;
}

hypergraph *hypergraph_copy(hypergraph *g)
{
    hypergraph *c = (hypergraph *)malloc(sizeof(hypergraph));

    *c = (hypergraph){.n = g->n, .m = g->m};

    c->Vd = (NodeID *)malloc(sizeof(NodeID *) * c->n);
    c->Va = (NodeID *)malloc(sizeof(NodeID *) * c->n);
    c->V = (NodeID **)malloc(sizeof(NodeID *) * c->n);

    for (NodeID i = 0; i < c->n; i++)
    {
        c->Vd[i] = g->Vd[i];
        c->Va[i] = g->Va[i];
        c->V[i] = (NodeID *)malloc(sizeof(int) * c->Va[i]);

        for (NodeID j = 0; j < c->Vd[i]; j++)
            c->V[i][j] = g->V[i][j];
    }

    if (g->Nd)
    {
        c->Nd = (NodeID *)malloc(sizeof(NodeID *) * c->n);
        c->Na = (NodeID *)malloc(sizeof(NodeID *) * c->n);
        c->N = (NodeID **)malloc(sizeof(NodeID *) * c->n);

        for (NodeID i = 0; i < c->n; i++)
        {
            c->Nd[i] = g->Nd[i];
            c->Na[i] = g->Na[i];
            c->N[i] = (NodeID *)malloc(sizeof(int) * c->Na[i]);

            for (NodeID j = 0; j < c->Nd[i]; j++)
                c->N[i][j] = g->N[i][j];
        }
    }
    else
    {
        c->Nd = NULL;
        c->Na = NULL;
        c->N = NULL;
    }

    c->Ed = (NodeID *)malloc(sizeof(int *) * c->m);
    c->Ea = (NodeID *)malloc(sizeof(int *) * c->m);
    c->E = (NodeID **)malloc(sizeof(int *) * c->m);

    for (NodeID i = 0; i < c->m; i++)
    {
        c->Ed[i] = g->Ed[i];
        c->Ea[i] = g->Ea[i];
        c->E[i] = (NodeID *)malloc(sizeof(int) * c->Ea[i]);

        for (NodeID j = 0; j < c->Ed[i]; j++)
            c->E[i][j] = g->E[i][j];
    }

    return c;
}

bool hypergraph_validate(hypergraph *g)
{
    for (NodeID i = 0; i < g->n; i++)
        if (!hypergraph_validate_vertex(g, i))
            return 0;

    for (NodeID i = 0; i < g->m; i++)
        if (!hypergraph_validate_edge(g, i))
            return 0;

    return 1;
}

bool hypergraph_validate_edge(hypergraph *g, NodeID e)
{
    for (NodeID i = 0; i < g->Ed[e]; i++)
    {
        NodeID v = g->E[e][i];
        if (v < 0 || v >= g->n || g->Ea[e] < g->Ed[e])
            return 0;
        if (i > 0 && v <= g->E[e][i - 1])
            return 0;

        NodeID p = lower_bound(g->V[v], g->Vd[v], e);
        if (p == g->Vd[v] || g->V[v][p] != e)
            return 0;
    }

    return 1;
}

bool hypergraph_validate_vertex(hypergraph *g, NodeID u)
{
    for (NodeID i = 0; i < g->Vd[u]; i++)
    {
        NodeID e = g->V[u][i];
        if (e >= g->m || g->Va[u] < g->Vd[u])
            return 0;
        if (i > 0 && e <= g->V[u][i - 1])
            return 0;

        NodeID p = lower_bound(g->E[e], g->Ed[e], u);
        if (p == g->Ed[e] || g->E[e][p] != u)
            return 0;
    }

    if (g->Nd)
    {
        for (NodeID i = 0; i < g->Nd[u]; i++)
        {
            NodeID v = g->N[u][i];
            if (v == u)
                return 0;
            if (v >= g->n || g->Na[u] < g->Nd[u])
                return 0;
            if (i > 0 && v <= g->N[u][i - 1])
                return 0;

            NodeID p = lower_bound(g->N[u], g->Nd[u], v);
            if (p == g->Nd[u] || g->N[u][p] != v)
                return 0;
        }
    }
    return 1;
}

void hypergraph_free(hypergraph *g)
{
    free(g->Vd);
    free(g->Va);
    free(g->Nd);
    free(g->Na);
    for (NodeID i = 0; i < g->n; i++)
    {
        free(g->V[i]);
        if (g->N)
            free(g->N[i]);
    }

    free(g->V);
    free(g->N);

    free(g->Ed);
    free(g->Ea);
    for (NodeID i = 0; i < g->m; i++)
        free(g->E[i]);
    free(g->E);

    free(g);
}