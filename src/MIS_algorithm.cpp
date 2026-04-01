#include "MIS_algorithm.h"

MISH_algorithm::MISH_algorithm(hypergraph *hgr) : status(hgraph_status(hgr)), edge_marker(status.m), node_set(hgr->n), edge_set(hgr->m)
{
    start_time = std::chrono::high_resolution_clock::now();
    status.reductions = make_reduction_vector<degree_one_reduction, twin_reduction, sunflower_reduction, clique_reduction, node_domination_reduction, unconfined_reduction>(status.n);

    reduction_map.resize(REDUCTION_NUM);

    for (size_t i = 0; i < status.reductions.size(); i++)
        reduction_map[status.reductions[i]->get_reduction_type()] = i;

    if (EXPERIMENT)
    {
        n_reduced.resize(REDUCTION_NUM + 1);
        m_reduced.resize(REDUCTION_NUM + 1);
        t_reduced.resize(REDUCTION_NUM + 1);
        for (size_t i = 0; i < status.reductions.size() + 1; i++)
        {
            n_reduced[i] = 0;
            m_reduced[i] = 0;
            t_reduced[i] = 0.0;
        }
    }
    if (hgr->m < hgr->n)
        edge_set = fast_set(hgr->n);
}
MISH_algorithm::~MISH_algorithm()
{
    clear_reduction_vector(status.reductions);
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
        for (NodeID i = 0; i < g->Nd[u]; i++)
        {
            NodeID v = g->N[u][i];
            if (status.node_status[v] == IS_status::not_set)
            {
                set(v, IS_status::excluded);
                i--;
            }
        }

        for (NodeID e = 0; e < g->Vd[u]; e++)
        {
            NodeID edge = g->V[u][e];
            hypergraph_remove_edge(g, edge, &node_set, true);
            status.remaining_edges--;
            status.edge_status[edge] = false;
        }
    }
    else
    {
        add_next_level_neighborhood(u);
        for (NodeID e = 0; e < g->Vd[u]; e++)
        {
            NodeID edge = g->V[u][e];
            if (g->Ed[edge] <= 2)
            {
                hypergraph_remove_edge(g, edge, &node_set, true);
                status.remaining_edges--;
                status.edge_status[edge] = false;
            }
        }
    }

    hypergraph_remove_vertex(status.hgraph, u);
    return;
}

void MISH_algorithm::init_reduction_step()
{
    if (!status.reductions[active_reduction_index]->has_run)
    {
        status.reductions[active_reduction_index]->marker.fill_current_ascending(status.n);
        status.reductions[active_reduction_index]->marker.clear_next();
        status.reductions[active_reduction_index]->has_run = true;
    }
    else
    {
        // reduction has already run, only consider nodes whose neighborhood was changed before
        status.reductions[active_reduction_index]->marker.get_next();
    }
}

void MISH_algorithm::reduce_graph()
{
    bool progress = false;
    bool edge_run = false;

    do
    {
        progress = false;
        NodeID prev_n = status.remaining_nodes;
        NodeID prev_m = status.remaining_edges;
        std::chrono::high_resolution_clock::time_point prev_time = std::chrono::high_resolution_clock::now();

        for (auto &reduction : status.reductions)
        {
            double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
            if (elapsed > TIME_KERNEL_SECONDS * 1000)
                break;

            active_reduction_index = reduction_map[reduction->get_reduction_type()];
            init_reduction_step();
            progress = reduction->reduce(this);

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

                    if (VERBOSE)
                        // std::cout << n_reduced[reduction->get_reduction_type()] << " \t" << m_reduced[reduction->get_reduction_type()] << " \t" << t_reduced[reduction->get_reduction_type()] << "\t" << reduction->get_reduction_name() << std::endl;
                        std::cout << n_reduced[reduction->get_reduction_type()] << " \t" << m_reduced[reduction->get_reduction_type()] << " \t" << t_reduced[reduction->get_reduction_type()] << "\t" << reduction->get_reduction_name() << status.remaining_nodes << "," << status.remaining_edges << std::endl;
                    break;
                }
            }

            active_reduction_index++;
        }

        if (!progress)
        {
            if (edge_run)
            {
                edge_marker.get_next();
            }
            else
            {
                edge_marker.fill_current_ascending(status.m);
                edge_marker.clear_next();
                edge_run = true;
            }
            progress = remove_dominating_edges();

            if (EXPERIMENT)
            {
                t_reduced[REDUCTION_NUM] += std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - prev_time).count();
                prev_time = std::chrono::high_resolution_clock::now();

                if (progress)
                {
                    n_reduced[REDUCTION_NUM] += prev_n - status.remaining_nodes;
                    m_reduced[REDUCTION_NUM] += prev_m - status.remaining_edges;

                    prev_n = status.remaining_nodes;
                    prev_m = status.remaining_edges;

                    if (VERBOSE)
                        std::cout << n_reduced[REDUCTION_NUM] << " \t" << m_reduced[REDUCTION_NUM] << " \t" << t_reduced[REDUCTION_NUM] << "\t" << "edge_domination" << std::endl;
                }
            }
        }

    } while (progress && status.remaining_nodes > 0);

    return;
}

hypergraph *MISH_algorithm::build_reduced_hypergraph(hypergraph *g, std::vector<NodeID> &remap, std::vector<bool> &sol)
{
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
        if (reduction->has_run)
            reduction->marker.add(v);
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
        edge_marker.add(e);
        for (NodeID j = 0; j < status.hgraph->Ed[e]; j++)
        {
            NodeID u = status.hgraph->E[e][j];
            add_next_level_node(u);
        }
    }
}

bool MISH_algorithm::remove_dominating_edges()
{
    auto g = status.hgraph;
    NodeID old_e = status.remaining_edges;

    // for (NodeID i = 0; i < edge_marker.current_size(); ++i)
    // {
    //     NodeID e = edge_marker.current_element(i);

    for (NodeID e = 0; e < status.hgraph->m; ++e)
    {
        if (g->Ed[e] > EDGE_SIZE || !status.edge_status[e])
            continue;

        if (g->Ed[e] <= 1)
        {
            hypergraph_remove_edge(g, e, &node_set, true);
            status.remaining_edges--;
            status.edge_status[e] = false;
            continue;
        }
        assert(!(g->Ed[e] == 0 && status.edge_status[e]));

        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - start_time).count();
        if (elapsed > TIME_KERNEL_SECONDS)
            break;

        NodeID net1 = e;

        // minPin is node in net1 with min degree
        NodeID minPin = g->E[net1][0];
        for (NodeID j = 1; j < g->Ed[net1]; j++)
        {
            NodeID u = g->E[net1][j];
            if (g->Vd[minPin] > g->Vd[u])
                minPin = u;
        }

        bool is_dominated = false;

        // iterate over all other incident edges of minPin that are not net1 and larger equal net1
        for (NodeID j = 0; j < g->Vd[minPin]; j++)
        {
            NodeID net2 = g->V[minPin][j];
            if (net2 == net1 || g->Ed[net2] < g->Ed[net1])
                continue;
            // check if all hypernodes of net1 are contained in net2
            is_dominated = true;
            NodeID *p_net1 = g->E[net1];
            NodeID *p_net2 = g->E[net2];

            while (p_net1 != g->E[net1] + g->Ed[net1])
            {
                while (*p_net1 != *p_net2)
                {
                    p_net2++;
                    // if p-net2 reaches the end of the pins -> no domination
                    if (p_net2 == g->E[net2] + g->Ed[net2])
                    {
                        is_dominated = false;
                        break;
                    }
                }
                if (!is_dominated)
                    break;
                else
                    p_net1++;
            }
            if (is_dominated)
            {
                add_next_level_nodes_of_edge(net1);
                hypergraph_remove_edge(g, net1, &node_set, true);
                status.remaining_edges--;
                status.edge_status[net1] = false;
                break;
            }
        }

        if (old_e - status.remaining_edges >= NUM_REMOVED_EDGES)
            break;
    }
    return old_e != status.remaining_edges;
}