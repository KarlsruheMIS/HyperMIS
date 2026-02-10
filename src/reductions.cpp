#include "hypergraph.h"
#include "reductions.h"
#include "MIS_algorithm.h"
#include <numeric>
#include <unordered_set>
#include <algorithm>
#include <cassert>

typedef MISH_algorithm::IS_status IS_status;

// Test if A and B are equal
static inline int set_is_equal(const NodeID *A, NodeID a, const NodeID *B, NodeID b)
{
    if (b != a)
        return 0;

    for (NodeID i = 0; i < a; i++)
    {
        if (A[i] != B[i])
            return 0;
    }

    return 1;
}

// Test if A is a subset of B
static inline int set_is_subset(const NodeID *A, NodeID a, const NodeID *B, NodeID b)
{
    if (b < a)
        return 0;

    NodeID i = 0, j = 0;
    while (i < a && j < b && (a - i <= b - j))
    {
        if (A[i] == B[j])
        {
            i++;
            j++;
        }
        else if (A[i] > B[j])
        {
            j++;
        }
        else
        {
            return 0;
        }
    }

    return i == a;
}

// Test if A is a subset of B, ignoring x from A
static inline int set_is_subset_except_one(const NodeID *A, NodeID a, const NodeID *B, NodeID b, NodeID x)
{
    if (b < a - 1)
        return 0;

    NodeID i = 0, j = 0;
    while (i < a && j < b)
    {
        if (A[i] == B[j])
        {
            i++;
            j++;
        }
        else if (A[i] > B[j])
        {
            j++;
        }
        else if (A[i] == x)
        {
            i++;
        }
        else
        {
            return 0;
        }
    }

    if (i < a && A[i] == x)
        i++;

    return i == a;
}

bool degree_one_reduction::reduce(MISH_algorithm *mish_alg)
{
  auto &status = mish_alg->status;
  NodeID old_n = status.remaining_nodes;

  for (size_t v_idx = 0; v_idx < marker.current_size(); v_idx++)
  {
    NodeID v = marker.current_element(v_idx);

    if (status.node_status[v] == IS_status::not_set && status.hgraph->Vd[v] < 2)
        mish_alg->set(v, IS_status::included);
  }

  return old_n != status.remaining_nodes;
}

bool unconfined_reduction::reduce(MISH_algorithm *mish_alg)
{
  auto &status = mish_alg->status;
  NodeID old_n = status.remaining_nodes;
  auto g = status.hgraph;

  std::vector<NodeID> S;
  std::vector<NodeID> neighborhood_S;
  neighborhood_S.reserve(status.n);
  fast_set extend_neighborhood_S(status.n);
  int num_iterations = 0;

  for (size_t v_idx = 0; v_idx < marker.current_size(); v_idx++)
  {
  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - mish_alg->start_time).count();
  if (elapsed > TIME_KERNEL_SECONDS)
      break;
    NodeID v = marker.current_element(v_idx);
    if (status.node_status[v] != IS_status::not_set)
      continue;

    if (num_iterations >= ITERATIONS_UNCONFINED)
      break;

    if (g->Nd[v] > 20)
      continue;

    bool v_confined = false;

    S.clear();
    neighborhood_S.clear();
    extend_neighborhood_S.clear();

    NodeID next_node = v;
    while (!v_confined && status.node_status[v] == IS_status::not_set && S.size() <= CONSTANT_UNCONFINED)
    {
      auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - mish_alg->start_time).count();
      if (elapsed > TIME_KERNEL_SECONDS)
        break;

      S.push_back(next_node);
      extend_neighborhood_S.add(next_node);

      for (NodeID i = 0; i < g->Nd[v]; i++)
      {
        NodeID neighbor = g->N[v][i];
        if (g->Nd[neighbor] > NEIGHBORS_SIZE)
          continue;

        neighborhood_S.push_back(neighbor);
        extend_neighborhood_S.add(neighbor);
      }

      bool found_unconfined = false;
      v_confined = true;
      for (NodeID n = 0; n < neighborhood_S.size(); n++)
      {
        NodeID u = neighborhood_S[n];
        if (found_unconfined || g->Nd[u] > 20)
          continue;
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - mish_alg->start_time).count();
        if (elapsed > TIME_KERNEL_SECONDS)
          break;

        // calculate number of intersected nodes
        short intersection_size = 0;
        int i = 0;
        while (intersection_size <= 2 && i < S.size())
        {
          NodeID s = S[i];
          if (hypergraph_is_neighbor(g, u, s))
            intersection_size++;

          i++;
        }

        if (intersection_size == 1)
        {
          // boolean if there exists a node with intersection == 1
          // calculate difference: N(u) \ N[S]
          NodeID difference_node;
          short difference_size = 0;
          for (NodeID i = 0; i < g->Nd[u]; i++)
          {
            NodeID v = g->N[u][i];
            if (!extend_neighborhood_S.get(v))
            {
              difference_size++;
              if (difference_size == 2)
                break;

              difference_node = v;
            }
          }

          if (difference_size == 1)
          {
            next_node = difference_node;
            v_confined = false;
          }
          else if (difference_size == 0)
          {
            v_confined = false;
            found_unconfined = true;
          }
        }
      }
      // in this case, v is unconfined
      if (found_unconfined)
        mish_alg->set(v, IS_status::excluded);
    }
    num_iterations++;
  }

  return old_n != status.remaining_nodes;
}

bool sunflower_reduction::reduce(MISH_algorithm *mish_alg)
{
  auto &status = mish_alg->status;
  NodeID old_n = status.remaining_nodes;
  hypergraph *g = status.hgraph;

  for (size_t v_idx = 0; v_idx < marker.current_size(); v_idx++)
  {
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - mish_alg->start_time).count();
    if (elapsed > TIME_KERNEL_SECONDS)
      break;

    NodeID v = marker.current_element(v_idx);
    if (status.node_status[v] != IS_status::not_set)
      continue;
    bool is_sunflower = false;

    for (NodeID i = 0; i < g->Nd[v]; i++)
    {
      NodeID u = g->N[v][i];
      if (status.node_status[u] != IS_status::not_set || g->Vd[v] != g->Vd[u])
        continue;
      is_sunflower = set_is_equal(g->V[v], g->Vd[v], g->V[u], g->Vd[u]);

      if (is_sunflower)
      {
        mish_alg->set(v, IS_status::excluded);
        break;
      }
    }
  }

  return old_n != status.remaining_nodes;
}

bool node_domination_reduction::reduce(MISH_algorithm *mish_alg)
{
  auto &status = mish_alg->status;
  NodeID old_n = status.remaining_nodes;
  hypergraph *g = status.hgraph;

  for (size_t v_idx = 0; v_idx < marker.current_size(); v_idx++)
  {
    NodeID v = marker.current_element(v_idx);

    if (status.node_status[v] == IS_status::not_set)
    {
      auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - mish_alg->start_time).count();
      if (elapsed > TIME_KERNEL_SECONDS)
        break;

      if (g->Nd[v] > NEIGHBORS_SIZE || g->Nd[v] == 0)
        continue;

      for (NodeID i = 0; i < g->Nd[v]; i++)
      {
        NodeID u = g->N[v][i];
        if (g->Vd[u] == 0)
          continue;

        bool is_dominating = set_is_subset_except_one(g->N[v], g->Nd[v], g->N[u], g->Nd[u], u); 

        if (is_dominating)
        {
          mish_alg->set(u, IS_status::excluded);
          break;
        }
      }
    }
  }
  return old_n != status.remaining_nodes;
}

bool twin_reduction::reduce(MISH_algorithm *mish_alg)
{
  auto &status = mish_alg->status;
  NodeID old_n = status.remaining_nodes;
  hypergraph *g = status.hgraph;
  std::vector<NodeID> twins;

  for (size_t v_idx = 0; v_idx < marker.current_size(); v_idx++)
  {
    NodeID v = marker.current_element(v_idx);

    if (status.node_status[v] != IS_status::not_set || g->Nd[v] > 50)
      continue;

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - mish_alg->start_time).count();
    if (elapsed > TIME_KERNEL_SECONDS)
      break;

    twins.clear();
    twins.push_back(v);
    NodeID minD_neighbor = g->N[v][0];
    for (NodeID i = 1; i < g->Nd[v]; i++)
    {
      NodeID u = g->N[v][i];
      assert(status.node_status[u]==IS_status::not_set);
      if (g->Vd[minD_neighbor] > g->Vd[u])
        minD_neighbor = u;
    }

    for (NodeID i = 0; i < g->Nd[minD_neighbor]; i++)
    {
      NodeID t1 = g->N[minD_neighbor][i];

      if (g->Nd[t1] != g->Nd[v] || t1 == v || status.node_status[t1] != IS_status::not_set)
        continue;

      if (hypergraph_is_neighbor(g, t1, v))
        continue;

      bool is_twin = set_is_equal(g->N[v], g->Nd[v], g->N[t1], g->Nd[t1]);

      if (is_twin)
      {
        twins.push_back(t1);
      }
    }

    // reduce found twins
    NodeID min_degree = g->Vd[twins[0]];
    for (NodeID i = 1; i < twins.size(); i++)
      if (min_degree > g->Vd[twins[i]])
        min_degree = g->Vd[twins[i]];
    
    if (twins.size() >= min_degree)
      for (NodeID twin : twins)
        mish_alg->set(twin, IS_status::included);
  }

  return old_n != status.remaining_nodes;
}

bool clique_reduction::reduce(MISH_algorithm *mish_alg)
{
  auto &status = mish_alg->status;
  hypergraph *g = status.hgraph;
  NodeID old_n = status.remaining_nodes;

  for (size_t v_idx = 0; v_idx < marker.current_size(); v_idx++)
  {
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - mish_alg->start_time).count();
    if (elapsed > TIME_KERNEL_SECONDS)
      break;

    NodeID v = marker.current_element(v_idx);
    if (status.node_status[v] != IS_status::not_set || g->Vd[v] != 2 || g->Nd[v] > NEIGHBORS_SIZE || g->Nd[v] < 2)
      continue;

    bool simplicial = true;
    for (NodeID i = 0; i < g->Nd[v]; i++)
    {
      NodeID u = g->N[v][i];
      simplicial = set_is_subset_except_one(g->N[v], g->Nd[v], g->N[u],g->Nd[u], u);
      if (!simplicial)
        break;
    }

    if (simplicial)
    {
      mish_alg->set(v, IS_status::included);
      break;
    }
  }
  return old_n != status.remaining_nodes;
}

void clear_reduction_vector(std::vector<reduction_ptr> &vec)
{
	for (auto &rp : vec)
		rp.release();
	vec.clear();
}