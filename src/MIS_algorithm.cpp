#include "MIS_algorithm.h"

MISH_algorithm::MISH_algorithm(hypergraph *hgr) : status(hgraph_status(hgr)), edge_marker(status.m), node_set(hgr->n), edge_set(hgr->m)
{
    start_time = std::chrono::high_resolution_clock::now();
    if (UNCONFINED_REDUCE)
        status.reductions = make_reduction_vector<degree_one_reduction, twin_reduction, sunflower_reduction, clique_reduction, node_domination_reduction, unconfined_reduction>(status.n);
    else
        status.reductions = make_reduction_vector<degree_one_reduction, twin_reduction, sunflower_reduction, clique_reduction, node_domination_reduction>(status.n);

    reduction_map.resize(REDUCTION_NUM);

    for (size_t i = 0; i < status.reductions.size(); i++)
    {
        reduction_map[status.reductions[i]->get_reduction_type()] = i;
    }
    if (hgr->m < hgr->n)
        edge_set = fast_set(hgr->n);
}
MISH_algorithm::~MISH_algorithm() {
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
    }
    else
    {
        add_next_level_neighborhood(u);
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

        for (auto &reduction : status.reductions)
        {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - start_time).count();
            if (elapsed > TIME_KERNEL_SECONDS)
                break;

            active_reduction_index = reduction_map[reduction->get_reduction_type()];
            init_reduction_step();
            progress = reduction->reduce(this);

            if (progress)
            {
                if (VERBOSE)
                    std::cout << status.remaining_nodes << " \t" << status.remaining_edges << " \t" << reduction->get_reduction_name() << std::endl;
                break;
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

            if (progress && VERBOSE)
                std::cout << status.remaining_nodes << " \t" << status.remaining_edges << " \tdominating edge" << std::endl;
        }

        if (!progress && HEURISTIC_RED > 0)
        {
            if (H_EXCLUDE)
                heuristic_reduction_maxnei();
            else
                heuristic_reduction_minnei();

            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - start_time).count();
            if (elapsed > TIME_KERNEL_SECONDS)
                break;
            progress = true;
        }
    } while (progress && status.remaining_nodes > 0);

    return;
}

hypergraph *MISH_algorithm::build_reduced_hypergraph(hypergraph *g, std::vector<NodeID> &map, std::vector<NodeID> &remap, std::vector<bool> & sol)
{
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

void MISH_algorithm::heuristic_reduction_maxnei()
{
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> seed_dist(0, 1000000);
    int random_seed = seed_dist(rng);
    std::mt19937 local_rng(random_seed);
    std::uniform_real_distribution<double> noise_dist(0.9, 1.1);

    hypergraph *g = status.hgraph;
    if (status.remaining_nodes > 0 && pq_reduce_and_peel.empty())
    {
        // fill pq 
        NodeID max_Nd = 0;
        for (NodeID v = 0; v < g->n; v++)
        {
            if (status.node_status[v] != IS_status::not_set)
                continue;
            if (max_Nd < g->Nd[v])
                max_Nd = g->Nd[v];
        }
            
        for (NodeID v = 0; v < g->n; v++)
        {
            if (status.node_status[v] != IS_status::not_set)
                continue;
            
            double noise = noise_dist(local_rng);
            pq_reduce_and_peel.insert(v, max_Nd-g->Nd[v]*noise);
        }
    }

    size_t iterations = (HEURISTIC_RED * (size_t)status.remaining_nodes ) / 1000;
    if (1 > iterations) 
        iterations = 1;
    for (int i = 0; i < iterations; i++)
    { 
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - start_time).count();
        if (elapsed > TIME_KERNEL_SECONDS)
            break;

        if (!pq_reduce_and_peel.empty())
        {
            NodeID u = pq_reduce_and_peel.deleteMin();
            if (status.node_status[u] != IS_status::not_set)
                continue;

            for (NodeID i = 0; i < g->Nd[u]; i++)
            {
                NodeID neighbor = g->N[u][i];
                if (pq_reduce_and_peel.contains(neighbor))
                    pq_reduce_and_peel.increaseKey(neighbor,g->Nd[neighbor]+1);
            }
            set(u, IS_status::excluded);
        }
    }
}

void MISH_algorithm::heuristic_reduction_minnei()
{
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> seed_dist(0, 1000000);
    int random_seed = seed_dist(rng);
    std::mt19937 local_rng(random_seed);
    std::uniform_real_distribution<double> noise_dist(0.9, 1.1);

    hypergraph *g = status.hgraph;
    if (status.remaining_nodes > 0 && pq_reduce_and_peel.empty())
    {
        // fill pq 
        for (NodeID v = 0; v < g->n; v++)
        {
            if (status.node_status[v] != IS_status::not_set)
                continue;
            
            double noise = noise_dist(local_rng);
            pq_reduce_and_peel.insert(v, g->Nd[v] * noise);
        }
    }

    size_t iterations = (HEURISTIC_RED * (size_t)status.remaining_nodes ) / 1000;
    if (1 > iterations) 
        iterations = 1;
    for (int i = 0; i < iterations; i++)
    { 
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - start_time).count();
        if (elapsed > TIME_KERNEL_SECONDS)
            break;

        if (!pq_reduce_and_peel.empty())
        {
            NodeID u = pq_reduce_and_peel.deleteMin();
            if (status.node_status[u] != IS_status::not_set)
                continue;

            for (NodeID i = 0; i < g->Nd[u]; i++)
            {
                NodeID neighbor = g->N[u][i];
                pq_reduce_and_peel.deleteNode(neighbor);
                for (NodeID j = 0; j < g->Nd[neighbor]; j++)
                {
                    NodeID second_neighbor = g->N[neighbor][j];
                    if (pq_reduce_and_peel.contains(second_neighbor))
                        pq_reduce_and_peel.decreaseKey(second_neighbor,g->Nd[second_neighbor]-1);
                }
            }
            set(u, IS_status::included);
        }
    }
}

void MISH_algorithm::printIS()
{
    std::cout << "nodes in the IS: ";
    for (int i = 0; i < status.n; ++i)
    {
        if (status.node_status[i] == IS_status::included)
        {
            std::cout << i << ", ";
        }
    }
    std::cout << std::endl;
}

bool MISH_algorithm::remove_dominating_edges()
{
    auto g = status.hgraph;
    NodeID old_e = status.remaining_edges;

    for (NodeID i = 0; i < edge_marker.current_size(); ++i)
    {
        NodeID e = edge_marker.current_element(i);
        if (g->Ed[e] > EDGE_SIZE)
            continue;

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
                break;
            }
        } 


        if (old_e - status.remaining_edges >= NUM_REMOVED_EDGES)
            break;
    }
    return old_e != status.remaining_edges;
}

std::pair<int, double> greedy_loop(const hypergraph *g, double max_time_sec, bool loop, std::vector<bool> &sol)
{

    NodeID n = g->n;
    auto start_time = std::chrono::high_resolution_clock::now();
    NodeID best_size = 0;
    if (n == 0)
        return {0, 0.0};

    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> seed_dist(0, 1000000);
    double elapsed_seconds = 0.0;
    double time_found_best = 0.0;

    minNodeHeap *pq = new minNodeHeap();
    std::vector<bool> mark(n, 0);
    std::vector<bool> MIS_heu(n, 0);

    if (loop)
    {
        while (elapsed_seconds < max_time_sec)
        {
            int random_seed = seed_dist(rng);
            std::mt19937 local_rng(random_seed);
            std::uniform_real_distribution<double> noise_dist(0.9, 1.1);
            assert(pq->empty() && "priority queue has to be empty here");

            for (NodeID v = 0; v < n; v++)
            {
                double noise = noise_dist(local_rng);
                pq->insert(v, g->Nd[v] * noise);
            }

            NodeID size = greedy(g, pq, mark, MIS_heu);
            auto elapsed_time = std::chrono::high_resolution_clock::now() - start_time;
            if (size > best_size)
            {
                best_size = size;
                time_found_best = elapsed_time.count();
                for (NodeID i = 0; i < MIS_heu.size(); i++)
                {
                    sol[i] = MIS_heu[i];
                }
            }

            std::fill(mark.begin(), mark.end(), false);
            std::fill(MIS_heu.begin(), MIS_heu.end(), false);
        }
    }
    else
    {
        for (NodeID v = 0; v < n; v++)
            pq->insert(v, g->Nd[v]);
        
        best_size = greedy(g, pq, mark, MIS_heu);
        std::chrono::duration<double> elapsed_time = std::chrono::high_resolution_clock::now() - start_time;
        time_found_best = elapsed_time.count();
        for (NodeID i = 0; i < MIS_heu.size(); i++)
            sol[i] = MIS_heu[i];
    }

    return std::make_pair(best_size, time_found_best);
}

NodeID greedy(const hypergraph *g, minNodeHeap *pq, std::vector<bool> &mark, std::vector<bool> &MIS_heu)
{
    NodeID size = 0;
    while (!pq->empty())
    {
        NodeID v = pq->deleteMin();
        if (!mark[v])
        {
            // mark node and add it to the heuristic MIS
            mark[v] = true;
            MIS_heu[v] = true;
            size++;
            // mark neighbors
            for (NodeID i = 0; i < g->Nd[v]; i++)
            {
                NodeID u = g->N[v][i];
                if (!mark[u])
                {
                    mark[u] = true;
                    pq->deleteNode(u);
                }
                // update neighbors neighbor
                for (NodeID j = 0; j < g->Nd[u]; j++)
                {
                    NodeID w = g->N[v][i];
                    if (!mark[w])
                    {
                        size_t neighbor_key = pq->getKey(w);
                        pq->decreaseKey(w, neighbor_key - 1);
                    }
                }
            }
        }
    }
    return size;
}
