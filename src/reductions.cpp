#include "hypergraph.h"
#include "reductions.h"
#include "MIS_algorithm.h"
#include <numeric>
#include <algorithm>
#include <cassert>

typedef MISH_algorithm::IS_status IS_status;

// True when g->N[v]/g->Nd[v] may be read directly. In on-demand mode only
// populated entries are readable; the rest fall back to the incidence walk.
static inline bool nbrs_ready(const hypergraph *g, NodeID v)
{
  return g->has_neighbors && (!g->on_demand || g->N_valid[v]);
}

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

bool node_degree_one_reduction::reduce(MISH_algorithm *mish_alg)
{
  auto &status = mish_alg->status;
  auto &neighbors = mish_alg->node_vec;
  auto &nodes = mish_alg->node_set;
  auto &g = mish_alg->status.hgraph;
  NodeID old_n = status.remaining_nodes;

  for (size_t v_idx = 0; v_idx < marker.current_size(); v_idx++)
  {
    NodeID v = marker.current_element(v_idx);

    if (status.node_status[v] != IS_status::not_set)
      continue;

    if (status.hgraph->Vd[v] < 2)
    {
      mish_alg->set(v, IS_status::included);
    }
    else if (nbrs_ready(g, v))
    {
      if (g->Nd[v] < 2)
        mish_alg->set(v, IS_status::included);
    }
  }

  return old_n != status.remaining_nodes;
}

bool edge_degree_one_reduction::reduce(MISH_algorithm *mish_alg)
{
  auto &status = mish_alg->status;
  NodeID old_e = status.remaining_edges;

  for (size_t e_idx = 0; e_idx < marker.current_size(); e_idx++)
  {
    NodeID e = marker.current_element(e_idx);

    if (status.edge_status[e] && status.hgraph->Ed[e] <= 1)
    {
      mish_alg->remove_edge(e);
      hypergraph_remove_size_one_edge(status.hgraph, e);
    }
  }

  return old_e != status.remaining_edges;
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
  NodeID *S = mish_alg->edge_vec;

  for (size_t v_idx = 0; v_idx < marker.current_size(); v_idx++)
  {
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - mish_alg->start_time).count();
    if (elapsed > TIME_KERNEL_SECONDS)
      break;

    if (num_iterations >= ITERATIONS_UNCONFINED)
      break;

    NodeID v = marker.current_element(v_idx);

    if (status.node_status[v] != IS_status::not_set)
      continue;

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

      if (nbrs_ready(g, next_node))
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
            if (g->Vd[neighbor] > MAX_DEGREE)
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

    if (old_n - status.remaining_nodes > 10)
    {
      // we return early, so carry the unscanned tail of our own marker over to
      // the next round; the other reductions' markers must not be touched
      for (size_t remaining_idx = v_idx + 1; remaining_idx < marker.current_size(); remaining_idx++)
      {
        NodeID v_remaining = marker.current_element(remaining_idx);
        marker.add(v_remaining);
      }
      return true;
    }
  }

  return old_n != status.remaining_nodes;
}

bool fast_node_domination_reduction::reduce(MISH_algorithm *mish_alg)
{
  auto &status = mish_alg->status;
  NodeID old_n = status.remaining_nodes;
  hypergraph *g = status.hgraph;

  for (size_t v_idx = 0; v_idx < marker.current_size(); v_idx++)
  {
    NodeID v = marker.current_element(v_idx);

    if (status.node_status[v] != IS_status::not_set)
      continue;

    if (nbrs_ready(g, v))
      continue;

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - mish_alg->start_time).count();
    if (elapsed > TIME_KERNEL_SECONDS)
      break;

    if (g->Vd[v] == 0)
    {
      mish_alg->set(v, IS_status::included);
      continue;
    }

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
      if (status.node_status[u] != IS_status::not_set || u == v)
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

  // node_domination is the workhorse that most benefits from a wider reach, so
  // it scales the degree / neighborhood-size bounds by NODE_DOM_FACTOR.
  const NodeID dom_max_degree = MAX_DEGREE * NODE_DOM_FACTOR;
  const NodeID dom_neighbors_size = NEIGHBORS_SIZE * NODE_DOM_FACTOR;

  for (size_t v_idx = 0; v_idx < marker.current_size(); v_idx++)
  {
    NodeID v = marker.current_element(v_idx);

    if (status.node_status[v] != IS_status::not_set)
      continue;

    if (g->Vd[v] > dom_max_degree)
      continue;

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - mish_alg->start_time).count();
    if (elapsed > TIME_KERNEL_SECONDS)
      break;

    NodeID dominated = 0;
    NodeID deg_v;
    NodeID *n = hypergraph_get_neighborhood(g, v, neighbors, deg_v, mish_alg->node_set);
    if (deg_v > dom_neighbors_size)
      continue;

    if (deg_v <= 1)
    {
      mish_alg->set(v, IS_status::included);
      continue;
    }

    for (NodeID i = 0; i < deg_v; i++)
    {
      NodeID u = n[i];
      // n may hold an already-decided vertex left stale in the cache. Drop it
      // from the live neighborhood (so it is never seen again) and re-examine
      // this slot. On the on-the-fly path n is a private buffer, so leave it.
      if (status.node_status[u] != IS_status::not_set)
      {
        if (g->has_neighbors)
        {
          hypergraph_remove_element(g->N[v], g->Nd[v], u);
          deg_v--;
          i--;
        }
        continue;
      }
      if (g->Vd[u] == 0 || g->Vd[u] > dom_max_degree)
        continue;

      NodeID deg_u;
      NodeID *nu = hypergraph_get_neighborhood(g, u, neighbors_u, deg_u, node_set);
      if (deg_u < deg_v - dominated)
        continue;

      bool is_dominating = set_is_subset_except_one(n, deg_v, nu, deg_u, u);

      if (is_dominating)
      {
        mish_alg->set(u, IS_status::excluded);

        if (status.hgraph->has_neighbors)
        { // n is the live g->N[v] (-f or on-demand); it shrank when u was removed
          i--;
          deg_v--;
        }
        else
        { // n is a private on-the-fly buffer: it does not shrink
          dominated++;
        }

        if (deg_v - dominated == 1)
        {
          mish_alg->set(v, IS_status::included);
          break;
        }
      }
    }
  }
  return old_n != status.remaining_nodes;
}

bool edge_domination_reduction::reduce(MISH_algorithm *mish_alg)
{
  auto &status = mish_alg->status;
  NodeID old_e = status.remaining_edges;
  hypergraph *g = status.hgraph;
  NodeID *dominated_edges = mish_alg->edge_vec;
  NodeID de_size = 0;

  for (size_t e_idx = 0; e_idx < marker.current_size(); e_idx++)
  {
    NodeID e = marker.current_element(e_idx);

    if (status.edge_status[e] == false)
      continue;

    if (g->Ed[e] <= 1)
    {
      mish_alg->remove_edge(e);
      dominated_edges[de_size++] = e;
      continue;
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - mish_alg->start_time).count();
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
      if (net2 == net1 || !status.edge_status[net2])
        continue;

      bool is_dominated = set_is_subset(g->E[net1], g->Ed[net1], g->E[net2], g->Ed[net2]);

      if (is_dominated)
      {
        dominated_edges[de_size++] = net1;
        mish_alg->remove_edge(net1);
        break;
      }
    }
  }

  if (de_size > 0)
    hypergraph_remove_edges(g, dominated_edges, de_size, &mish_alg->edge_set, &mish_alg->node_set);

  return old_e != status.remaining_edges;
}

bool twin_reduction::reduce(MISH_algorithm *mish_alg)
{
  auto &status = mish_alg->status;
  hypergraph *g = status.hgraph;
  NodeID old_n = status.remaining_nodes;

  auto &node_v = mish_alg->node_set;
  auto &node_t = mish_alg->edge_set;
  auto &neighbors = mish_alg->node_vec;
  auto &vec = mish_alg->node_vec2;
  auto &vec2 = mish_alg->edge_vec;

  std::vector<NodeID> twins;

  for (size_t v_idx = 0; v_idx < marker.current_size(); v_idx++)
  {
    NodeID v = marker.current_element(v_idx);

    if (status.node_status[v] != IS_status::not_set)
      continue;

    NodeID deg_v = g->Vd[v];
    if (deg_v > MAX_DEGREE)
      continue;

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - mish_alg->start_time).count();
    if (elapsed > TIME_KERNEL_SECONDS)
      break;

    NodeID dv;
    NodeID *nv = hypergraph_get_neighborhood_and_set(g, v, neighbors, dv, node_v);
    if (dv == 0)
    {
      mish_alg->set(v, IS_status::included);
      continue;
    }

    // Drop any already-decided vertices left stale in v's neighborhood (keeping
    // node_v in sync), then pick the smallest-degree live neighbor whose
    // neighborhood is scanned for twin candidates.
    NodeID minD_n = 0;
    bool have_min = false;
    for (size_t i = 0; i < dv;)
    {
      NodeID u = nv[i];
      if (status.node_status[u] != IS_status::not_set)
      {
        if (g->has_neighbors)
          hypergraph_remove_element(g->N[v], g->Nd[v], u);
        node_v.remove(u);
        dv--;
        continue; // array shifted; re-check slot i
      }
      if (!have_min || g->Vd[minD_n] > g->Vd[u])
      {
        minD_n = u;
        have_min = true;
      }
      i++;
    }
    if (!have_min)
    {
      mish_alg->set(v, IS_status::included);
      continue;
    }

    NodeID NminD;
    NodeID *t_cand = hypergraph_get_neighborhood(g, minD_n, vec, NminD, node_t);

    if (NminD > NEIGHBORS_SIZE)
      continue;

    twins.clear();
    twins.push_back(v);

    for (NodeID i = 0; i < NminD; i++)
    {
      NodeID t = t_cand[i];
      if (node_v.get(t))
        continue;
      if (status.node_status[t] != IS_status::not_set)
      {
        // decided vertex left stale in minD_n's neighborhood; drop it
        if (g->has_neighbors)
        {
          hypergraph_remove_element(g->N[minD_n], g->Nd[minD_n], t);
          NminD--;
          i--;
        }
        continue;
      }

      NodeID dt;
      NodeID *nt = hypergraph_get_neighborhood(g, t, vec2, dt, node_t);

      bool is_twin = set_is_equal(nv, dv, nt, dt);

      if (is_twin)
        twins.push_back(t);
    }

    // reduce found twins
    NodeID min_degree = g->Vd[twins[0]];
    for (NodeID i = 1; i < twins.size(); i++)
      if (min_degree > g->Vd[twins[i]])
        min_degree = g->Vd[twins[i]];

    if (twins.size() >= min_degree)
    {
      // store all includable twins and mark them and their neighbors as processed to be inlcuded at the end
      for (NodeID twin : twins)
        mish_alg->set(twin, IS_status::included);

      // we return early, so carry the unscanned tail of our own marker over to
      // the next round; the other reductions' markers must not be touched
      for (size_t remaining_idx = v_idx + 1; remaining_idx < marker.current_size(); remaining_idx++)
      {
        NodeID v_remaining = marker.current_element(remaining_idx);
        marker.add(v_remaining);
      }
      return true;
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