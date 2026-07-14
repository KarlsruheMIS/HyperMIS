#include "hypergraph.h"

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
    g->has_neighbors = 0;
    g->on_demand = 0;
    g->N_valid = NULL;
    g->nbr_scratch = NULL;

    g->Vd = (NodeID *)malloc(sizeof(NodeID) * n);
    g->Va = (NodeID *)malloc(sizeof(NodeID) * n);

    g->Ed = (NodeID *)malloc(sizeof(NodeID) * m);
    g->Ea = (NodeID *)malloc(sizeof(NodeID) * m);

    g->Nd = NULL;
    g->Na = NULL;
    g->N = NULL;

    g->V = (NodeID **)malloc(sizeof(NodeID *) * n);
    g->E = (NodeID **)malloc(sizeof(NodeID *) * m);

    for (NodeID i = 0; i < n; i++)
    {
        g->Vd[i] = 0;
        g->Va[i] = MIN_ALLOC;
        g->V[i] = (NodeID *)malloc(sizeof(NodeID) * g->Va[i]);
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

static void hypergraph_on_demand_heal(hypergraph *g, NodeID u);

NodeID *hypergraph_get_neighborhood(hypergraph *g, NodeID u, NodeID *neighborhood, NodeID &deg, fast_set &node_set)
{
    if (g->has_neighbors)
    {
        // on-demand: heal on miss, then return the live array (zero-copy, like
        // -f). Entries are kept consistent by vertex-removal patching; edge
        // removals may leave a stale neighbor, exactly as under -f.
        if (g->on_demand && !g->N_valid[u])
            hypergraph_on_demand_heal(g, u);
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

NodeID *hypergraph_get_neighborhood_and_set(hypergraph *g, NodeID u, NodeID *neighborhood, NodeID &deg, fast_set &node_set)
{
    node_set.clear();
    node_set.add(u);

    if (g->has_neighbors)
    {
        if (g->on_demand && !g->N_valid[u])
            hypergraph_on_demand_heal(g, u);
        for (NodeID i = 0; i < g->Nd[u]; i++)
        {
            NodeID v = g->N[u][i];
            node_set.add(v);
        }
        node_set.add(u);
        deg = g->Nd[u];
        return g->N[u];
    }
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
    if (g->has_neighbors)
        return;

    g->Nd = (NodeID *)malloc(sizeof(NodeID) * g->n);
    g->Na = (NodeID *)malloc(sizeof(NodeID) * g->n);
    g->N = (NodeID **)malloc(sizeof(NodeID *) * g->n);
    g->has_neighbors = 1;

    for (NodeID v = 0; v < g->n; v++)
    {
        g->Nd[v] = 0;
        g->Na[v] = MIN_ALLOC;
        g->N[v] = (NodeID *)malloc(sizeof(NodeID) * g->Na[v]);

        fs->clear();
        fs->add(v);
        for (NodeID i = 0; i < g->Vd[v]; i++)
        {
            NodeID e = g->V[v][i];
            for (NodeID j = 0; j < g->Ed[e]; j++)
            {
                NodeID u = g->E[e][j];
                if (fs->add(u))
                    hypergraph_append_element(g->Nd + v, g->Na + v, g->N + v, u);
            }
        }
        std::sort(g->N[v], g->N[v] + g->Nd[v]);
    }
}

void hypergraph_init_on_demand(hypergraph *g)
{
    if (g->has_neighbors)
        return;

    g->Nd = (NodeID *)malloc(sizeof(NodeID) * g->n);
    g->Na = (NodeID *)malloc(sizeof(NodeID) * g->n);
    g->N = (NodeID **)malloc(sizeof(NodeID *) * g->n);
    g->N_valid = (bool *)malloc(sizeof(bool) * g->n);

    for (NodeID v = 0; v < g->n; v++)
    {
        g->Nd[v] = 0;
        g->Na[v] = 0;   // buffer allocated lazily on first store
        g->N[v] = NULL;
        g->N_valid[v] = false;
    }

    g->nbr_scratch = new fast_set(g->n);
    g->on_demand = true;
    g->has_neighbors = true;
}

// on-demand self-heal: (re)build the current neighborhood of u into g->N[u] from
// incidence, exactly as hypergraph_build_neighbors does per vertex. Uses the
// private nbr_scratch so the caller's node_set is never disturbed.
static void hypergraph_on_demand_heal(hypergraph *g, NodeID u)
{
    g->Nd[u] = 0;
    if (g->N[u] == NULL)
    {
        g->Na[u] = MIN_ALLOC;
        g->N[u] = (NodeID *)malloc(sizeof(NodeID) * g->Na[u]);
    }

    fast_set *fs = g->nbr_scratch;
    fs->clear();
    fs->add(u);
    for (NodeID i = 0; i < g->Vd[u]; i++)
    {
        NodeID e = g->V[u][i];
        for (NodeID j = 0; j < g->Ed[e]; j++)
        {
            NodeID v = g->E[e][j];
            if (fs->add(v))
                hypergraph_append_element(g->Nd + u, g->Na + u, g->N + u, v);
        }
    }
    std::sort(g->N[u], g->N[u] + g->Nd[u]);
    g->N_valid[u] = true;
}

// type=0 g->V  type=1 g->E  type=2 g->N
void hypergraph_reset(hypergraph *g, NodeID v, int type)
{
    switch (type)
    {
    case 0:
        g->Vd[v] = 0;
        if (g->Va[v] == MIN_ALLOC)
            break;
        g->Va[v] = MIN_ALLOC;
        g->V[v] = (NodeID *)realloc(g->V[v], sizeof(NodeID) * g->Va[v]);
        break;
    case 1:
        g->Ed[v] = 0;
        if (g->Ea[v] == MIN_ALLOC)
            break;
        g->Ea[v] = MIN_ALLOC;
        g->E[v] = (NodeID *)realloc(g->E[v], sizeof(NodeID) * g->Ea[v]);
        break;
    case 2:
        g->Nd[v] = 0;
        if (g->Na[v] == MIN_ALLOC)
            break;
        g->Na[v] = MIN_ALLOC;
        g->N[v] = (NodeID *)realloc(g->N[v], sizeof(NodeID) * g->Na[v]);
        break;
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

// Like hypergraph_remove_element but tolerates absence. In on-demand mode two
// neighbor lists may have been built at different times, so u is not guaranteed
// to be present in a neighbor's list; a blind remove_element would corrupt it.
static inline void hypergraph_remove_element_if_present(NodeID *vec, NodeID &size, NodeID element)
{
    NodeID p = lower_bound(vec, size, element);
    if (p < size && vec[p] == element)
    {
        memmove(vec + p, vec + p + 1, sizeof(NodeID) * (size - p - 1));
        size--;
    }
}

void hypergraph_remove_vertex(hypergraph *g, NodeID u, fast_set *processed)
{
    if (g->has_neighbors && !g->on_demand)
    {
        for (NodeID i = 0; i < g->Nd[u]; i++)
        {
            NodeID v = g->N[u][i];
            hypergraph_remove_element(g->N[v], g->Nd[v], u);
        }
        hypergraph_reset(g, u, 2);
    }
    else if (g->on_demand)
    {
        // patch populated neighbor lists in place: drop u from every neighbor v
        // whose entry is built. Enumerate u's neighbors from its own list if
        // built, else from current incidence.
        if (g->N_valid[u])
        {
            for (NodeID i = 0; i < g->Nd[u]; i++)
            {
                NodeID v = g->N[u][i];
                if (g->N_valid[v])
                    hypergraph_remove_element_if_present(g->N[v], g->Nd[v], u);
            }
        }
        else
        {
            fast_set *fs = g->nbr_scratch;
            fs->clear();
            fs->add(u);
            for (NodeID i = 0; i < g->Vd[u]; i++)
            {
                NodeID e = g->V[u][i];
                for (NodeID j = 0; j < g->Ed[e]; j++)
                {
                    NodeID v = g->E[e][j];
                    if (fs->add(v) && g->N_valid[v])
                        hypergraph_remove_element_if_present(g->N[v], g->Nd[v], u);
                }
            }
        }
        g->Nd[u] = 0;
        g->N_valid[u] = false;
    }

    for (NodeID i = 0; i < g->Vd[u]; i++)
    {
        NodeID e = g->V[u][i];

        hypergraph_remove_element(g->E[e], g->Ed[e], u);

        if (g->Ed[e] == 1)
            hypergraph_remove_size_one_edge(g, e);
    }

    hypergraph_reset(g, u, 0);
}

void hypergraph_remove_size_one_edge(hypergraph *g, NodeID e)
{
    for (NodeID i = 0; i < g->Ed[e]; i++)
    {
        NodeID v = g->E[e][i];
        hypergraph_remove_element(g->V[v], g->Vd[v], e);
    }

    hypergraph_reset(g, e, 1);
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
    }

    // deferred to here: the loop above reads E[e] and Ed[e] of every removed edge
    for (NodeID i = 0; i < e_size; i++)
        hypergraph_reset(g, E[i], 1);
}

// remove edges and vertices (include u operation)
void hypergraph_remove_neighborhood(hypergraph *g, NodeID u, fast_set *nodes, fast_set *processed, fast_set *edges, NodeID *changed)
{
    // get vertices and edges that will be deleted to skip fixing their neighborhoods
    nodes->clear();
    edges->clear();
    nodes->add(u);
    processed->clear();
    processed->add(u);
    if (g->on_demand)
        g->N_valid[u] = false; // u is included and removed from the graph

    for (NodeID i = 0; i < g->Vd[u]; i++)
    {
        NodeID e = g->V[u][i];
        edges->add(e);

        for (NodeID j = 0; j < g->Ed[e]; j++)
        {
            NodeID v = g->E[e][j];
            nodes->add(v);
            if (g->on_demand)
                g->N_valid[v] = false; // neighbors of u are excluded and removed
        }
    }

    // changed[] collects the surviving 2-hop vertices
    NodeID changed_size = 0;
    if (g->has_neighbors && !g->on_demand)
    {
        for (NodeID i = 0; i < g->Nd[u]; i++)
        {
            NodeID d = g->N[u][i]; // excluded, hence removed from the graph
            for (NodeID j = 0; j < g->Nd[d]; j++)
            {
                NodeID w = g->N[d][j];
                if (!nodes->get(w))
                    hypergraph_remove_element_if_present(g->N[w], g->Nd[w], d);
            }
        }
    }
    else if (g->on_demand)
    {
        fast_set *v_seen = g->nbr_scratch;
        v_seen->clear();
        for (NodeID i = 0; i < g->Vd[u]; i++)
        {
            NodeID e = g->V[u][i];
            for (NodeID j = 0; j < g->Ed[e]; j++)
            {
                NodeID v = g->E[e][j]; // u or a neighbor of u
                if (!v_seen->add(v))
                    continue;

                for (NodeID k = 0; k < g->Vd[v]; k++)
                {
                    NodeID f = g->V[v][k]; // v's incident edges
                    if (edges->get(f))     // skip u's own (deleted) edges
                        continue;

                    for (NodeID l = 0; l < g->Ed[f]; l++)
                    {
                        NodeID w = g->E[f][l];
                        if (processed->add(w) && !nodes->get(w))
                            changed[changed_size++] = w;
                    }
                }
            }
        }
    }

    // on-demand only: a deleted vertex's own list may never have been built, so the
    // walk above cannot start from it -- the survivors are collected by incidence
    // instead, and each one's (populated) list is stripped in one pass.
    for (NodeID i = 0; i < changed_size; i++)
    {
        NodeID v = changed[i];
        if (g->N_valid[v])
            hypergraph_remove_set(g->N[v], g->Nd[v], nodes);
    }

    processed->clear();
    processed->add(u);
    for (NodeID i = 0; i < g->Vd[u]; i++)
    {
        NodeID e = g->V[u][i];
        for (NodeID j = 0; j < g->Ed[e]; j++)
        {
            NodeID v = g->E[e][j];

            if (!processed->add(v))
                continue;

            for (NodeID k = 0; k < g->Vd[v]; k++)
            {
                NodeID f = g->V[v][k];
                if (!edges->add(f))
                    continue;
                hypergraph_remove_set(g->E[f], g->Ed[f], nodes);
                if (g->Ed[f] == 1)
                    hypergraph_remove_size_one_edge(g, f);
            }
            hypergraph_reset(g, v, 0); // excluded vertices
        }
        hypergraph_reset(g, e, 1);
    }
    hypergraph_reset(g, u, 0);
    // assert(hypergraph_validate(g));
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

    // In on-demand mode g->N is only partially populated; the reduced graph does
    // not need a neighbor array (the ILP solver / output use incidence), so skip
    // rebuilding it and leave rg->has_neighbors = 0.
    if (g->has_neighbors && !g->on_demand)
    {

        rg->Nd = (NodeID *)malloc(sizeof(NodeID) * g->n);
        rg->Na = (NodeID *)malloc(sizeof(NodeID) * g->n);
        rg->N = (NodeID **)malloc(sizeof(NodeID *) * g->n);
        rg->has_neighbors = 1;

        for (NodeID u_new = 0; u_new < red_n; u_new++)
        {

            rg->Nd[u_new] = 0;
            rg->Na[u_new] = MIN_ALLOC;
            rg->N[u_new] = (NodeID *)malloc(sizeof(NodeID) * rg->Na[u_new]);

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

graph *hypergraph_clique_expansion(hypergraph *hg)
{
    NodeID *neighbors = (NodeID *)malloc(sizeof(NodeID) * hg->n);
    fast_set n_set(hg->n);
    NodeID m = 0;
    NodeID n = hg->n;

    for (NodeID v = 0; v < n; v++)
    {
        NodeID deg;
        NodeID *n = hypergraph_get_neighborhood(hg, v, neighbors, deg, n_set);
        m += deg;
    }

    long long *V = (long long *)malloc(sizeof(long long) * (n + 1));
    NodeID *E = (NodeID *)malloc(sizeof(NodeID) * (m));

    NodeID ei = 0;

    for (NodeID v = 0; v < n; v++)
    {
        V[v] = ei;
        NodeID deg;
        NodeID *n = hypergraph_get_neighborhood(hg, v, neighbors, deg, n_set);
        for (NodeID i = 0; i < deg; i++)
            E[ei++] = n[i];
    }
    V[n] = ei;

    graph *g = (graph *)malloc(sizeof(graph));
    *g = (graph){.n = n, .m = m, .V = V, .E = E};

    free(neighbors);
    return g;
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

    if (g->has_neighbors)
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

void hypergraph_free(hypergraph *g)
{
    free(g->Vd);
    free(g->Va);

    for (NodeID i = 0; i < g->n; i++)
        free(g->V[i]);

    if (g->has_neighbors)
    {
        free(g->Nd);
        free(g->Na);
        for (NodeID i = 0; i < g->n; i++)
            free(g->N[i]); // NULL-safe: on-demand entries never built stay NULL

        free(g->N);

        if (g->on_demand)
        {
            free(g->N_valid);
            delete g->nbr_scratch;
        }
    }


    free(g->V);

    free(g->Ed);
    free(g->Ea);
    for (NodeID i = 0; i < g->m; i++)
        free(g->E[i]);
    free(g->E);

    free(g);
}