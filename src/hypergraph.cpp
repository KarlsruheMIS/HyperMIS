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

    g->Nd = (NodeID *)malloc(sizeof(NodeID) * n);
    g->Na = (NodeID *)malloc(sizeof(NodeID) * n);

    g->V = (NodeID **)malloc(sizeof(NodeID *) * n);
    g->N = (NodeID **)malloc(sizeof(NodeID *) * n);
    g->E = (NodeID **)malloc(sizeof(NodeID *) * m);

    for (NodeID i = 0; i < n; i++)
    {
        g->Nd[i] = 0;
        g->Vd[i] = 0;
        g->Va[i] = MIN_ALLOC;
        g->Na[i] = MIN_ALLOC;
        g->V[i] = (NodeID *)malloc(sizeof(NodeID) * g->Va[i]);
        g->N[i] = (NodeID *)malloc(sizeof(NodeID) * g->Na[i]);
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

void hypergraph_build_neighbors(hypergraph *g, fast_set *fs)
{
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

        d = 0;
        for (NodeID j = 0; j < g->Nd[i]; j++)
        {
            if (j == 0 || g->N[i][j] > g->N[i][d - 1])
                g->N[i][d++] = g->N[i][j];
        }
        g->Nd[i] = d;
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

void hypergraph_free(hypergraph *g)
{
    free(g->Vd);
    free(g->Va);
    free(g->Nd);
    free(g->Na);
    for (NodeID i = 0; i < g->n; i++)
    {
        free(g->V[i]);
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
        return 1;
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


void hypergraph_remove_vertex(hypergraph *g, NodeID u)
{
    for (NodeID i = 0; i < g->Vd[u]; i++)
    {
        NodeID e = g->V[u][i];

        NodeID p = lower_bound(g->E[e], g->Ed[e], u);
        assert(p < g->Ed[e] && g->E[e][p] == u);
        memmove(g->E[e] + p, g->E[e] + p + 1, sizeof(NodeID) * (g->Ed[e] - p - 1));
        g->Ed[e]--;
    }

    g->Vd[u] = 0;

    for (NodeID i = 0; i < g->Nd[u]; i++)
    {
        NodeID v = g->N[u][i];
        assert(v != u);
        NodeID p = lower_bound(g->N[v], g->Nd[v], u);
        assert(p < g->Nd[v] && g->N[v][p] == u);
        memmove(g->N[v] + p, g->N[v] + p + 1, sizeof(NodeID) * (g->Nd[v] - p - 1));
        g->Nd[v]--;
    }
    g->Nd[u] = 0;
    g->Na[u] = MIN_ALLOC;
    g->N[u] = (NodeID *)realloc(g->N[u], sizeof(NodeID) * g->Na[u]);
}

void hypergraph_remove_neighbors(hypergraph *g, NodeID u, fast_set *fs, fast_set *efs)
{
    NodeID *new_N = (NodeID *)malloc(sizeof(NodeID) * g->n);

    fs->clear();
    efs->clear();
    fs->add(u);
    for (NodeID i = 0; i < g->Nd[u]; i++)
    {
        NodeID v = g->N[u][i];
        assert(hypergraph_validate_vertex(g, v));
        fs->add(v);
    }

    for (NodeID i = 0; i < g->Nd[u]; i++)
    {
        // update edge lists
        NodeID v = g->N[u][i];
        for (NodeID j = 0; j < g->Vd[v]; j++)
        {
            NodeID e = g->V[v][j];
            if (efs->get(e))
                continue;
            NodeID E_size = 0;
            for (NodeID k = 0; k < g->Ed[e]; k++)
            {
                NodeID w = g->E[e][k];
                if (fs->get(w))
                    continue;
                new_N[E_size++] = w;
            }
            for (NodeID k = 0; k < E_size; k++)
                g->E[e][k] = new_N[k];
            g->Ed[e] = E_size;

            assert(hypergraph_validate_edge(g, e));
            efs->add(e);
        }
    }

    efs->clear(); // used to mark the updated distance-2 neighbors
    for (NodeID i = 0; i < g->Nd[u]; i++)
    {
        NodeID v = g->N[u][i];
        efs->add(v);
    }
    efs->add(u);

    for (NodeID i = 0; i < g->Nd[u]; i++)
    {
        NodeID v = g->N[u][i];
        // update distance-2 neighbors
        for (NodeID j = 0; j < g->Nd[v]; j++)
        {
            NodeID z = g->N[v][j];
            if (!efs->get(z))
            {
                NodeID N_size = 0;
                for (NodeID k = 0; k < g->Nd[z]; k++)
                {
                    NodeID w = g->N[z][k];
                    if (fs->get(w)) // skip to be removed vertices
                        continue;
                    new_N[N_size++] = w;
                }
                for (NodeID k = 0; k < N_size; k++)
                    g->N[z][k] = new_N[k];

                g->Nd[z] = N_size;


                NodeID E_size = 0;
                for (NodeID k = 0; k < g->Vd[z]; k++)
                {
                    NodeID e = g->V[z][k];
                    new_N[E_size++] = e;
                }
                for (NodeID k = 0; k < E_size; k++)
                    g->V[z][k] = new_N[k];


                g->Vd[z] = E_size;

                efs->add(z);
                assert(hypergraph_validate_vertex(g, z));
            }
        }
    }

    // remove vertices
    for (NodeID i = 0; i < g->Nd[u]; i++)
    {
        NodeID v = g->N[u][i];
        g->Vd[v] = 0;
        g->Nd[v] = 0;
        g->Va[v] = MIN_ALLOC;
        g->Na[v] = MIN_ALLOC;
        g->V[v] = (NodeID *)realloc(g->V[v], sizeof(NodeID) * g->Va[v]);
        g->N[v] = (NodeID *)realloc(g->N[v], sizeof(NodeID) * g->Na[v]);
    }

    g->Nd[u] = 0;
    g->Na[u] = MIN_ALLOC;
    g->N[u] = (NodeID *)realloc(g->N[u], sizeof(NodeID) * g->Na[u]);

}

void hypergraph_remove_edge(hypergraph *g, NodeID e, fast_set *fs, bool dominated)
{
    for (NodeID i = 0; i < g->Ed[e]; i++)
    {
        NodeID v = g->E[e][i];

        NodeID p = lower_bound(g->V[v], g->Vd[v], e);
        assert(p < g->Vd[v] && g->V[v][p] == e);
        memmove(g->V[v] + p, g->V[v] + p + 1, sizeof(NodeID) * (g->Vd[v] - p - 1));
        g->Vd[v]--;
    }

    if (dominated)
    { // in this case neighborhood does not change
        g->Ed[e] = 0;
        g->Ea[e] = MIN_ALLOC;
        g->E[e] = (NodeID *)realloc(g->E[e], sizeof(NodeID) * g->Ea[e]);
        return;
    }

    for (NodeID i = 0; i < g->Ed[e]; i++)
    {
        fs->clear();
        NodeID v = g->E[e][i];
        for (NodeID j = 0; j < g->Vd[v]; j++)
        {
            NodeID f = g->V[v][j];
            if (f == e)
                continue;
            for (NodeID k = 0; k < g->Ed[f]; k++)
            {
                NodeID u = g->E[f][k];
                fs->add(u);
            }
        }

        NodeID n_count = 0;
        for (NodeID j = 0; j < g->Nd[v]; j++)
        {
            NodeID u = g->N[v][j];
            if (fs->get(u))
                continue;
            assert(j == lower_bound(g->N[v], g->Nd[v], u));
            memmove(g->N[v] + j, g->N[v] + j + 1, sizeof(NodeID) * (g->Nd[v] - j - 1));
            g->Nd[v]--;
            j--;
        }
    }

    g->Ed[e] = 0;
    g->Ea[e] = MIN_ALLOC;
    g->E[e] = (NodeID *)realloc(g->E[e], sizeof(NodeID) * g->Ea[e]);
}

bool hypergraph_is_neighbor(hypergraph *g, NodeID v, NodeID neighbor)
{
    NodeID p = lower_bound(g->N[v], g->Nd[v], neighbor);
    return p < g->Nd[v] && g->N[v][p] == neighbor;
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
    return rg;
}


hypergraph *hypergraph_copy(hypergraph *g)
{
    hypergraph *c = (hypergraph *)malloc(sizeof(hypergraph));

    *c = (hypergraph){.n = g->n, .m = g->m};

    c->Vd = (NodeID *)malloc(sizeof(NodeID *) * c->n);
    c->Va = (NodeID *)malloc(sizeof(NodeID *) * c->n);
    c->V  = (NodeID **)malloc(sizeof(NodeID *) * c->n);

    for (NodeID i = 0; i < c->n; i++)
    {
        c->Vd[i] = g->Vd[i];
        c->Va[i] = g->Va[i];
        c->V[i] = (NodeID *)malloc(sizeof(int) * c->Va[i]);

        for (NodeID j = 0; j < c->Vd[i]; j++)
            c->V[i][j] = g->V[i][j];
    }

    c->Nd = (NodeID *)malloc(sizeof(NodeID *) * c->n);
    c->Na = (NodeID *)malloc(sizeof(NodeID *) * c->n);
    c->N  = (NodeID **)malloc(sizeof(NodeID *) * c->n);

    for (NodeID i = 0; i < c->n; i++)
    {
        c->Nd[i] = g->Nd[i];
        c->Na[i] = g->Na[i];
        c->N[i] = (NodeID *)malloc(sizeof(int) * c->Na[i]);

        for (NodeID j = 0; j < c->Nd[i]; j++)
            c->N[i][j] = g->N[i][j];
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