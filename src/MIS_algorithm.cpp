#include "MIS_algorithm.h"

MISH_algorithm::MISH_algorithm(hypergraph *hgr) : status(hgraph_status(hgr)), node_set(hgr->n), node_set2(hgr->n), edge_set(hgr->m), requeue_node_set(hgr->n), requeue_edge_set(hgr->m)
{
    start_time = std::chrono::high_resolution_clock::now();
    switch (REDUCTION_CONFIG)
    {
    case 0:
        REDUCE = 0;
        break;

    // Single-reduction runs (1-8): exactly one rule, isolated effect
    case 1: // edge_size
        status.reductions = make_reduction_vector<edge_size_reduction>(status.n, status.m);
        break;
    case 2: // node_degree_one
        status.reductions = make_reduction_vector<node_degree_one_reduction>(status.n, status.m);
        break;
    case 3: // simplicial
        status.reductions = make_reduction_vector<simplicial_reduction>(status.n, status.m);
        break;
    case 4: // fast_node_domination
        status.reductions = make_reduction_vector<fast_node_domination_reduction>(status.n, status.m);
        break;
    case 5: // edge_domination
        status.reductions = make_reduction_vector<edge_domination_reduction>(status.n, status.m);
        break;
    case 6: // node_domination
        status.reductions = make_reduction_vector<node_domination_reduction>(status.n, status.m);
        break;
    case 7: // twin
        status.reductions = make_reduction_vector<twin_reduction>(status.n, status.m);
        break;
    case 8: // unconfined
        status.reductions = make_reduction_vector<unconfined_reduction>(status.n, status.m);
        break;

    // Full pipeline (9): all rules in application order 
    case 9:
        status.reductions = make_reduction_vector<edge_size_reduction, node_degree_one_reduction, simplicial_reduction, fast_node_domination_reduction, edge_domination_reduction, node_domination_reduction, twin_reduction, unconfined_reduction>(status.n, status.m);
        break;

    // Disable-one from full (10-17) 
    case 10: // no edge_size
        status.reductions = make_reduction_vector<node_degree_one_reduction, simplicial_reduction, fast_node_domination_reduction, edge_domination_reduction, node_domination_reduction, twin_reduction, unconfined_reduction>(status.n, status.m);
        break;
    case 11: // no node_degree_one
        status.reductions = make_reduction_vector<edge_size_reduction, simplicial_reduction, fast_node_domination_reduction, edge_domination_reduction, node_domination_reduction, twin_reduction, unconfined_reduction>(status.n, status.m);
        break;
    case 12: // no simplicial
        status.reductions = make_reduction_vector<edge_size_reduction, node_degree_one_reduction, fast_node_domination_reduction, edge_domination_reduction, node_domination_reduction, twin_reduction, unconfined_reduction>(status.n, status.m);
        break;
    case 13: // no fast_node_domination
        status.reductions = make_reduction_vector<edge_size_reduction, node_degree_one_reduction, simplicial_reduction, edge_domination_reduction, node_domination_reduction, twin_reduction, unconfined_reduction>(status.n, status.m);
        break;
    case 14: // no edge_domination
        status.reductions = make_reduction_vector<edge_size_reduction, node_degree_one_reduction, simplicial_reduction, fast_node_domination_reduction, node_domination_reduction, twin_reduction, unconfined_reduction>(status.n, status.m);
        break;
    case 15: // no node_domination
        status.reductions = make_reduction_vector<edge_size_reduction, node_degree_one_reduction, simplicial_reduction, fast_node_domination_reduction, edge_domination_reduction, twin_reduction, unconfined_reduction>(status.n, status.m);
        break;
    case 16: // no twin
        status.reductions = make_reduction_vector<edge_size_reduction, node_degree_one_reduction, simplicial_reduction, fast_node_domination_reduction, edge_domination_reduction, node_domination_reduction, unconfined_reduction>(status.n, status.m);
        break;
    case 17: // no unconfined
        status.reductions = make_reduction_vector<edge_size_reduction, node_degree_one_reduction, simplicial_reduction, fast_node_domination_reduction, edge_domination_reduction, node_domination_reduction, twin_reduction>(status.n, status.m);
        break;
    default:
        break;
    }

    // Make sure Include_Deg1 vertices only when degree one rule is enabled (for clean experimental setup).
    INCLUDE_DEG1 = 0;
    for (auto &r : status.reductions)
        if (r->get_reduction_type() == reduction_type::node_degree_one ||
            r->get_reduction_type() == reduction_type::simplicial)
            INCLUDE_DEG1 = 1;

    NodeID max_num_reductions = 8;
    NodeID reduction_num = status.reductions.size();
    reduction_map.resize(max_num_reductions);

    for (size_t i = 0; i < reduction_num; i++)
        reduction_map[status.reductions[i]->get_reduction_type()] = i;

    if (EXPERIMENT)
    {
        n_reduced.assign(max_num_reductions, 0);
        m_reduced.assign(max_num_reductions, 0);
        t_reduced.assign(max_num_reductions, 0.0);
    }
    if (hgr->m < hgr->n)
    {
        edge_set = fast_set(hgr->n);
        edge_vec = (NodeID *)malloc(sizeof(NodeID) * hgr->n);
    }
    else
    {
        edge_vec = (NodeID *)malloc(sizeof(NodeID) * hgr->m);
    }

    node_vec = (NodeID *)malloc(sizeof(NodeID) * hgr->n);
    node_vec2 = (NodeID *)malloc(sizeof(NodeID) * hgr->n);
}
MISH_algorithm::~MISH_algorithm()
{
    clear_reduction_vector(status.reductions);
    free(node_vec);
    free(node_vec2);
    free(edge_vec);
}

void MISH_algorithm::set(NodeID u, IS_status is_status)
{
    assert(status.node_status[u] == IS_status::not_set);
    auto g = status.hgraph;
    status.node_status[u] = is_status;
    status.remaining_nodes--;
    if (is_status == IS_status::included)
    {
        status.IS_size++;

        size_t incidence = 0;
        if (g->has_neighbors && !g->on_demand)
            incidence = g->Nd[u];
        else
            for (NodeID i = 0; i < g->Vd[u]; i++)
                incidence += g->Ed[g->V[u][i]];

        if (incidence < REQUEUE_BATCH_MIN)
        {
            if (g->has_neighbors && !g->on_demand)
            {
                for (NodeID i = 0; i < g->Nd[u]; i++)
                {
                    NodeID v = g->N[u][i];
                    if (status.node_status[v] == IS_status::not_set)
                    {
                        status.remaining_nodes--;
                        status.node_status[v] = IS_status::excluded;
                        add_next_level_neighborhood(v);
                    }
                }
            }
            else
            {
                for (NodeID i = 0; i < g->Vd[u]; i++)
                {
                    NodeID e = g->V[u][i];
                    for (NodeID j = 0; j < g->Ed[e]; j++)
                    {
                        NodeID v = g->E[e][j];
                        if (status.node_status[v] == IS_status::not_set)
                        {
                            status.remaining_nodes--;
                            status.node_status[v] = IS_status::excluded;
                            add_next_level_neighborhood(v);
                        }
                    }
                }
            }
        }
        else
        {
            // batched path
            if (g->has_neighbors && !g->on_demand)
            {
                for (NodeID i = 0; i < g->Nd[u]; i++)
                {
                    NodeID v = g->N[u][i];
                    if (status.node_status[v] == IS_status::not_set)
                    {
                        status.remaining_nodes--;
                        status.node_status[v] = IS_status::excluded;
                        collect_next_level_neighborhood(v);
                    }
                }
            }
            else
            {
                for (NodeID i = 0; i < g->Vd[u]; i++)
                {
                    NodeID e = g->V[u][i];
                    for (NodeID j = 0; j < g->Ed[e]; j++)
                    {
                        NodeID v = g->E[e][j];
                        if (status.node_status[v] == IS_status::not_set)
                        {
                            status.remaining_nodes--;
                            status.node_status[v] = IS_status::excluded;
                            collect_next_level_neighborhood(v);
                        }
                    }
                }
            }
            flush_next_level();
        }

        for (NodeID e = 0; e < g->Vd[u]; e++)
        {
            NodeID edge = g->V[u][e];
            status.remaining_edges--;
            status.edge_status[edge] = false;
        }

        hypergraph_remove_neighborhood(status.hgraph, u, &node_set, &node_set2, &edge_set, node_vec);
    }
    else
    {
        add_next_level_neighborhood(u);
        hypergraph_remove_vertex(g, u, &node_set);
    }
}

void MISH_algorithm::init_reduction_step()
{
    auto& reduction = status.reductions[active_reduction_index];
    if (!reduction->has_run)
    {
        if (reduction->vertex_rule)
            reduction->marker.fill_current_ascending(status.n);
        else
            reduction->marker.fill_current_ascending(status.m);

        reduction->marker.clear_next();
        reduction->has_run = true;
    }
    else
    {
        // reduction has already run, only consider nodes whose neighborhood was changed before
        status.reductions[active_reduction_index]->marker.get_next();
    }
}

void MISH_algorithm::reduce_graph()
{
    const size_t target_factor = NODE_DOM_FACTOR;
    std::vector<size_t> stages;
    if (target_factor > 1)
        stages = {1, target_factor};
    else
        stages = {target_factor};

    for (size_t stage = 0; stage < stages.size(); stage++)
    {
        NODE_DOM_FACTOR = stages[stage];
        if (stage > 0)
        {
            // widen the reach and re-scan the residual from scratch
            for (auto &reduction : status.reductions)
                reduction->has_run = false;
        }

    bool progress = false;

    do
    {
        progress = false;
        NodeID prev_n = status.remaining_nodes;
        NodeID prev_m = status.remaining_edges;
        std::chrono::high_resolution_clock::time_point prev_time = std::chrono::high_resolution_clock::now();

        for (int reduction_index = 0; reduction_index < status.reductions.size(); reduction_index++)
        {
            auto &reduction = status.reductions[reduction_index];
            double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
            if (elapsed > TIME_KERNEL_SECONDS * 1000)
                break;

            active_reduction_index = reduction_map[reduction->get_reduction_type()];

            init_reduction_step();
            progress = reduction->reduce(this);

            if (progress)
                reduction_index = -1;

            if (EXPERIMENT)
            {
                t_reduced[reduction->get_reduction_type()] += std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - prev_time).count();
                prev_time = std::chrono::high_resolution_clock::now();
                if (progress)
                {
                    n_reduced[reduction->get_reduction_type()] += prev_n - status.remaining_nodes;
                    m_reduced[reduction->get_reduction_type()] += prev_m - status.remaining_edges;

                    prev_n = status.remaining_nodes;
                    prev_m = status.remaining_edges;

                    break;
                }
            }
        }
    } while (progress && status.remaining_nodes > 0);
    }

    NODE_DOM_FACTOR = target_factor;
    return;
}

hypergraph *MISH_algorithm::build_reduced_hypergraph(hypergraph *g, std::vector<NodeID> &remap, std::vector<bool> &sol)
{
    // TODO use node_vecs
    std::vector<NodeID> map(g->n, -1);
    std::vector<int> reduced(status.n, 1);
    for (NodeID v = 0; v < g->n; v++)
    {
        if (status.node_status[v] == IS_status::not_set)
            reduced[v] = 0;
        if (status.node_status[v] == IS_status::included)
            sol[v] = true;

        assert(reduced[v] == 0 || g->Vd[v] == 0);
    }
    remap.resize(status.remaining_nodes);
    map.resize(status.n);

    return hypergraph_build_reduced(g, map.data(), remap.data(), reduced.data());
}

void MISH_algorithm::add_next_level_node(NodeID v)
{
    for (auto &reduction : status.reductions)
    {
        if (reduction->has_run && reduction->vertex_rule)
            reduction->marker.add(v);
    }
}

void MISH_algorithm::add_next_level_edge(NodeID e)
{
    for (auto &reduction : status.reductions)
    {
        if (reduction->has_run && !reduction->vertex_rule)
            reduction->marker.add(e);
    }
}

void MISH_algorithm::add_next_level_nodes_of_edge(NodeID e)
{
    for (NodeID i = 0; i < status.hgraph->Ed[e]; i++)
    {
        NodeID v = status.hgraph->E[e][i];
        add_next_level_node(v);
    }
}

void MISH_algorithm::add_next_level_neighborhood(NodeID v)
{
    for (NodeID i = 0; i < status.hgraph->Vd[v]; i++)
    {
        NodeID e = status.hgraph->V[v][i];
        add_next_level_edge(e);

        for (NodeID j = 0; j < status.hgraph->Ed[e]; j++)
        {
            NodeID u = status.hgraph->E[e][j];
            add_next_level_node(u);
        }
    }
}

// add_next_level_neighborhood, for when a whole neighborhood is removed at once
void MISH_algorithm::collect_next_level_neighborhood(NodeID v)
{
    for (NodeID i = 0; i < status.hgraph->Vd[v]; i++)
    {
        NodeID e = status.hgraph->V[v][i];
        if (requeue_edge_set.add(e))
            requeue_edges.push_back(e);
    }
}

void MISH_algorithm::flush_next_level()
{
    for (NodeID e : requeue_edges)
    {
        add_next_level_edge(e);

        for (NodeID j = 0; j < status.hgraph->Ed[e]; j++)
        {
            NodeID u = status.hgraph->E[e][j];
            if (requeue_node_set.add(u))
                requeue_nodes.push_back(u);
        }
    }

    for (NodeID u : requeue_nodes)
        add_next_level_node(u);

    requeue_edges.clear();
    requeue_nodes.clear();
    requeue_edge_set.clear();
    requeue_node_set.clear();
}

void MISH_algorithm::remove_edge(NodeID edge)
{
    add_next_level_nodes_of_edge(edge);
    status.edge_status[edge] = false;
    status.remaining_edges--;
}
