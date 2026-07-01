#include "hypergraph.h"
#include "reductions.h"
#include "MIS_algorithm.h"
#include <numeric>
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

// Test if A is a subset of B, ignoring x and negative numbers from A
static inline int set_is_subset_except_one_positive(const NodeID *A, NodeID a, const NodeID *B, NodeID b, NodeID x)
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
    else if (A[i] == x || A[i] < 0)
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

// Test if A \ B = \emtpyset (return -1) if A\B = {v} (return v) else return -2
static inline int set_difference_check(const NodeID *A, NodeID a, const NodeID *B, NodeID b)
{
  if (a > b + 1)
    return -2;

  int difference = 0;

  NodeID differenceNode;
  NodeID i = 0, j = 0;
  while (i < a && j < b && difference < 2)
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
    else if (A[i] < B[j])
    {
      differenceNode = A[i];
      difference++;
      i++;
    }
  }

  if (difference == 0 && i == a)
    return -1;

  if (difference == 1 && i == a)
    return differenceNode;

  return -2;
}

// Test if |A \cap B| == 1
static inline int set_intersection_equal_one(const NodeID *A, NodeID a, const NodeID *B, NodeID b)
{
  int intersection = 0;
  NodeID i = 0, j = 0;
  while (intersection < 2 && i < a && j < b)
  {
    if (A[i] == B[j])
    {
      intersection++;
      i++;
      j++;
    }
    else if (A[i] > B[j])
      j++;
    else
      i++;
  }

  if (intersection == 1)
    return 1;
  else
    return 0;
}

bool degree_one_reduction::reduce(MISH_algorithm *mish_alg)
{
  auto &status = mish_alg->status;
  auto &neighbors = mish_alg->node_vec;
  auto &nodes = mish_alg->node_set;
  auto &g = mish_alg->status.hgraph;
  NodeID old_n = status.remaining_nodes;

  for (size_t v_idx = 0; v_idx < marker.current_size(); v_idx++)
  {
    NodeID v = marker.current_element(v_idx);

    if (status.node_status[v] == IS_status::not_set)
    {
      if (status.hgraph->Vd[v] < 2)
      {
        mish_alg->set(v, IS_status::included);
      }
      else if (g->Nd && g->Nd[v] < 2)
      {
        mish_alg->set(v, IS_status::included);
      }
      else
      {
        NodeID Nd;
        NodeID *n = hypergraph_get_neighborhood(g, v, neighbors, Nd, nodes);
        if (Nd < 2)
          mish_alg->set(v, IS_status::included);
      }
    }
  }

  return old_n != status.remaining_nodes;
}

bool unconfined_reduction::reduce(MISH_algorithm *mish_alg)
{
  auto &status = mish_alg->status;
  NodeID old_n = status.remaining_nodes;
  auto g = status.hgraph;

  auto &neighbors = mish_alg->node_vec;
  auto &neighborhood_S = mish_alg->node_vec2;
  auto &extend_neighborhood_S = mish_alg->node_set;
  int num_iterations = 0;
  NodeID *S = (NodeID *)malloc(sizeof(NodeID) * CONSTANT_UNCONFINED);

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

    bool v_confined = false;

    NodeID S_size = 0;
    NodeID neighborhood_S_size = 0;
    extend_neighborhood_S.clear();

    NodeID next_node = v;
    while (!v_confined && S_size <= CONSTANT_UNCONFINED)
    {
      auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - mish_alg->start_time).count();
      if (elapsed > TIME_KERNEL_SECONDS)
        break;

      S[S_size++] = next_node;
      extend_neighborhood_S.add(next_node);
      neighborhood_S[neighborhood_S_size++] = next_node;

      if (g->Nd)
      {
        for (NodeID i = 0; i < g->Nd[next_node]; i++)
        {
          NodeID neighbor = g->N[next_node][i];
          if (extend_neighborhood_S.add(neighbor))
            neighborhood_S[neighborhood_S_size++] = neighbor;
        }
      }
      else
      {
        for (NodeID i = 0; i < g->Vd[next_node]; i++)
        {
          NodeID e = g->V[next_node][i];
          for (NodeID j = 0; j < g->Ed[e]; j++)
          {
            NodeID neighbor = g->E[e][j];
            if (g->Vd[neighbor] > NEIGHBORS_SIZE)
              continue;

            if (extend_neighborhood_S.add(neighbor))
              neighborhood_S[neighborhood_S_size++] = neighbor;
          }
        }
      }

      std::sort(neighborhood_S, neighborhood_S + neighborhood_S_size);
      std::sort(S, S + S_size);

      bool v_unconfined = false;
      bool u_is_child_of_S = false;

      NodeID u;
      NodeID Nd;
      NodeID *n;
      for (NodeID i = 0; i < neighborhood_S_size && !u_is_child_of_S; i++)
      {
        u = neighborhood_S[i];
        n = hypergraph_get_neighborhood(g, u, neighbors, Nd, mish_alg->node_set);
        u_is_child_of_S = set_intersection_equal_one(n, Nd, S, S_size);
      }

      if (!u_is_child_of_S)
      {
        v_confined = true;
      }
      else
      {
        int unconfined_condition_check = set_difference_check(n, Nd, neighborhood_S, neighborhood_S_size);

        if (unconfined_condition_check == -1)
          v_unconfined = true;
        else if (unconfined_condition_check < -1)
          v_confined = true;
        else
          next_node = unconfined_condition_check;
      }
      if (v_unconfined)
      {
        mish_alg->set(v, IS_status::excluded);
        break;
      }

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
    NodeID v = marker.current_element(v_idx);

    if (status.node_status[v] != IS_status::not_set)
      continue;

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - mish_alg->start_time).count();
    if (elapsed > TIME_KERNEL_SECONDS)
      break;

    if (g->Vd[v] > NEIGHBORS_SIZE || g->Vd[v] == 0)
      continue;

    NodeID e_min = g->V[v][0];
    for (NodeID i = 1; i < g->Vd[v]; i++)
    {
      NodeID e = g->V[v][i];
      if (g->Ed[e_min] > g->Ed[e])
        e_min = e;
    }

    for (NodeID i = 0; i < g->Ed[e_min]; i++)
    {
      NodeID u = g->E[e_min][i];
      if (status.node_status[u] != IS_status::not_set || u == v || g->Vd[v] > g->Vd[u])
        continue;

      bool is_dominating = set_is_subset(g->V[v], g->Vd[v], g->V[u], g->Vd[u]);

      if (is_dominating)
        mish_alg->set(u, IS_status::excluded);
    }
  }

  return old_n != status.remaining_nodes;
}

bool node_domination_reduction::reduce(MISH_algorithm *mish_alg)
{
  auto &status = mish_alg->status;
  auto &neighbors = mish_alg->node_vec;
  auto &node_set = mish_alg->node_set;
  auto &neighbors_u = mish_alg->node_vec2;
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

      NodeID dominated = 0;
      NodeID deg_v;
      NodeID *n = hypergraph_get_neighborhood(g, v, neighbors, deg_v, mish_alg->node_set);
      if (deg_v > NEIGHBORS_SIZE || deg_v == 0)
        continue;

      for (NodeID i = 0; i < deg_v; i++)
      {
        NodeID u = n[i];
        if (g->Vd[u] == 0)
          continue;

        NodeID deg_u;
        NodeID *nu = hypergraph_get_neighborhood(g, u, neighbors_u, deg_u, node_set);
        if (deg_u < deg_v - dominated)
          continue;

        bool is_dominating = set_is_subset_except_one_positive(n, deg_v, nu, deg_u, u);

        if (is_dominating)
        {
          mish_alg->set(u, IS_status::excluded);
          if (USE_NEIGHBORHOOD_ARRAY)
          {
            i--;
            deg_v--;
          }
          else
          {
            n[i] = -1;
            dominated++;
          }
        }
      }
    }
  }
  return old_n != status.remaining_nodes;
}

bool twin_reduction::reduce(MISH_algorithm *mish_alg)
{
  auto &status = mish_alg->status;
  auto &neighbors = mish_alg->node_vec;
  auto &ntwin = mish_alg->node_vec2;
  auto &node_set = mish_alg->node_set;
  NodeID old_n = status.remaining_nodes;
  hypergraph *g = status.hgraph;
  std::vector<NodeID> twins;
  NodeID *t_cand = (NodeID *)malloc(sizeof(NodeID) * status.hgraph->n);

  for (size_t v_idx = 0; v_idx < marker.current_size(); v_idx++)
  {
    NodeID v = marker.current_element(v_idx);

    if (status.node_status[v] != IS_status::not_set)
      continue;

    NodeID deg_v = g->Vd[v];
    if (deg_v > 50 || deg_v == 0)
      continue;

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - mish_alg->start_time).count();
    if (elapsed > TIME_KERNEL_SECONDS)
      break;

    twins.clear();
    twins.push_back(v);

    NodeID Nd;
    NodeID *n = hypergraph_get_neighborhood(g, v, neighbors, Nd, node_set);
    if (Nd == 0)
    {
      mish_alg->set(v, IS_status::included);
      continue;
    }

    NodeID minD_neighbor = n[0];
    for (size_t i = 1; i < Nd; i++)
    {
      NodeID u = n[i];
      assert(status.node_status[u] == IS_status::not_set);
      if (g->Vd[minD_neighbor] > g->Vd[u])
        minD_neighbor = u;
    }

    NodeID NminD;
    NodeID *minDn = hypergraph_get_neighborhood(g, minD_neighbor, t_cand, NminD, node_set);
    for (NodeID i = 0; i < NminD; i++)
    {
      NodeID t = minDn[i];
      NodeID NtD;
      NodeID *nt = hypergraph_get_neighborhood(g, t, ntwin, NtD, node_set);
      if (NtD != Nd || t == v || status.node_status[t] != IS_status::not_set)
        continue;

      bool adjacent = false;
      for (NodeID j = 0; j < NtD; j++)
      {
        if (nt[j] == v)
        {
          adjacent = true;
          break;
        }
      }
      if (adjacent)
        continue;

      bool is_twin = set_is_equal(n, Nd, nt, NtD);

      if (is_twin)
      {
        twins.push_back(t);
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

void clear_reduction_vector(std::vector<reduction_ptr> &vec)
{
  for (auto &rp : vec)
    rp.release();
  vec.clear();
}