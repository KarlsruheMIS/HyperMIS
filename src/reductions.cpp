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

bool simplicial_reduction::reduce(MISH_algorithm *mish_alg)
{
  auto &status = mish_alg->status;
  auto &nodes = mish_alg->node_set;
  auto &g = mish_alg->status.hgraph;
  NodeID old_n = status.remaining_nodes;

  static constexpr NodeID INVALID_EDGE = std::numeric_limits<NodeID>::max();
  NodeID marked_edge = INVALID_EDGE;

  for (size_t v_idx = 0; v_idx < marker.current_size(); v_idx++)
  {
    NodeID v = marker.current_element(v_idx);

    if (status.node_status[v] != IS_status::not_set)
      continue;

    if (g->Vd[v] < 2 && INCLUDE_DEG1)
    {
      mish_alg->set(v, IS_status::included);
      marked_edge = INVALID_EDGE;
      continue;
    }

    if (nbrs_ready(g, v))
    {
      bool progress = false;
      for (NodeID i = 0; i < g->Vd[v] && !progress; i++)
      {
        NodeID e = g->V[v][i];
        if (status.edge_status[e] && g->Ed[e] == g->Nd[v] + 1)
        {
          mish_alg->set(v, IS_status::included);
          marked_edge = INVALID_EDGE;
          progress = true;
        }
      }
      continue;
    }

    // on-demand mode: e spans N[v] iff every other incident edge is a subset of it (any such e must be the largest incident edge)
    NodeID e = INVALID_EDGE;
    for (NodeID i = 0; i < g->Vd[v]; i++)
    {
      NodeID f = g->V[v][i];
      if (!status.edge_status[f])
        continue;
      if (e == INVALID_EDGE || g->Ed[f] > g->Ed[e])
        e = f;
    }
    if (e == INVALID_EDGE)
      continue;

    if (e != marked_edge)
    {
      nodes.clear();
      for (NodeID j = 0; j < g->Ed[e]; j++)
        nodes.add(g->E[e][j]);
      marked_edge = e;
    }

    bool spans = true;
    for (NodeID i = 0; i < g->Vd[v] && spans; i++)
    {
      NodeID f = g->V[v][i];
      if (f == e || !status.edge_status[f])
        continue;
      if (g->Ed[f] > g->Ed[e])
      {
        spans = false;
        break;
      }
      for (NodeID j = 0; j < g->Ed[f]; j++)
      {
        if (!nodes.get(g->E[f][j]))
        {
          spans = false;
          break;
        }
      }
    }

    if (spans)
    {
      mish_alg->set(v, IS_status::included);
      marked_edge = INVALID_EDGE;
    }
  }

  return old_n != status.remaining_nodes;
}

bool edge_size_reduction::reduce(MISH_algorithm *mish_alg)
{
  auto &status = mish_alg->status;
  auto &nodes = mish_alg->node_set;
  auto &g = mish_alg->status.hgraph;
  NodeID old_e = status.remaining_edges;
  NodeID old_n = status.remaining_nodes;

  for (size_t e_idx = 0; e_idx < marker.current_size(); e_idx++)
  {
    NodeID e = marker.current_element(e_idx);

    if (!status.edge_status[e])
      continue;

    if (g->Ed[e] <= 1)
    {
      mish_alg->remove_edge(e);
      hypergraph_remove_size_one_edge(g, e);
    }
    else if (status.remaining_nodes == 0)
    {
      continue;
    }
    else if (g->Ed[e] == status.remaining_nodes)
    {
      // e spans the residual, and it is a clique
      for (NodeID j = 0; j < g->Ed[e]; j++)
      {
        NodeID v = g->E[e][j];
        if (status.node_status[v] == IS_status::not_set)
        {
          mish_alg->set(v, IS_status::included);
          return true;
        }
      }
    }
    else if (g->Ed[e] + 1 == status.remaining_nodes)
    {
      // exactly one vertex outside e, then it is simplicial (either degree 0 or N[u] subseteq e,
      // e a clique).
      nodes.clear();
      for (NodeID j = 0; j < g->Ed[e]; j++)
        nodes.add(g->E[e][j]);

      NodeID outside = 0;
      NodeID u = 0;
      for (NodeID w = 0; w < status.n && outside < 2; w++)
      {
        if (status.node_status[w] != IS_status::not_set || nodes.get(w))
          continue;
        u = w;
        outside++;
      }

      if (outside == 1)
        mish_alg->set(u, IS_status::included);
      if (g->Ed[e] > 0)
      {
        for (NodeID j = 0; j < g->Ed[e]; j++)
        {
          NodeID v = g->E[e][j];
          if (status.node_status[v] == IS_status::not_set)
          {
            mish_alg->set(v, IS_status::included);
            return true;
          }
        }
      }
    }
  }

  return old_e != status.remaining_edges || old_n != status.remaining_nodes;
}

bool unconfined_reduction::reduce(MISH_algorithm *mish_alg)
{
  auto &status = mish_alg->status;
  NodeID old_n = status.remaining_nodes;
  auto g = status.hgraph;

  auto &neighbors = mish_alg->node_vec;
  auto &neighborhood_S = mish_alg->node_vec2;
  auto &extend_neighborhood_S = mish_alg->node_set;

  for (size_t v_idx = 0; v_idx < marker.current_size(); v_idx++)
  {
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - mish_alg->start_time).count();
    if (elapsed > TIME_KERNEL_SECONDS)
      break;

    NodeID v = marker.current_element(v_idx);

    if (status.node_status[v] != IS_status::not_set)
      continue;

    bool v_unconfined = false;
    NodeID neighborhood_S_size = 0;
    NodeID S_size = 0; // |S|, only needed to gate the diamond extension below
    extend_neighborhood_S.clear();

    auto add_to_S = [&](NodeID s)
    {
      S_size++;
      extend_neighborhood_S.add(s);
      neighborhood_S[neighborhood_S_size++] = s;

      if (nbrs_ready(g, s))
      {
        for (NodeID i = 0; i < g->Nd[s]; i++)
        {
          NodeID neighbor = g->N[s][i];
          if (g->Vd[neighbor] > MAX_DEGREE)
            continue;
          intersection_count[neighbor]++;
          if (extend_neighborhood_S.add(neighbor))
            neighborhood_S[neighborhood_S_size++] = neighbor;
        }
      }
      else
      {
        mish_alg->node_set2.clear();
        for (NodeID i = 0; i < g->Vd[s]; i++)
        {
          NodeID e = g->V[s][i];
          for (NodeID j = 0; j < g->Ed[e]; j++)
          {
            NodeID neighbor = g->E[e][j];
            if (neighbor == s)
              continue;
            if (g->Vd[neighbor] > MAX_DEGREE)
              continue;
            if (!mish_alg->node_set2.add(neighbor))
              continue;

            intersection_count[neighbor]++;
            if (extend_neighborhood_S.add(neighbor))
              neighborhood_S[neighborhood_S_size++] = neighbor;
          }
        }
      }
    };

    add_to_S(v);

    // grow S until no child extends it. Each pass scans the children of S (|N(u) cap S| == 1)
    // a child with two or more vertices outside N[S] neither excludes v nor extends S but a later child still can so skip it and keep scanning
    bool ok = false;
    while (!ok && !v_unconfined)
    {
      auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - mish_alg->start_time).count();
      if (elapsed > TIME_KERNEL_SECONDS)
        break;

      ok = true;
      for (NodeID i = 0; i < neighborhood_S_size; i++)
      {
        NodeID u = neighborhood_S[i];

        if (intersection_count[u] != 1) // not a child of S
          continue;

        NodeID Nd;
        NodeID *n = hypergraph_get_neighborhood(g, u, neighbors, Nd, mish_alg->node_set2);

        // |N(u) \ N[S]|, capped at 2 -- we only ever distinguish 0, 1 and many
        // |N(u)\N[S]| >= |N(u)| - |N[S]|, so a child with Nd > |N[S]|+1 cannot have a difference below 2 and needs no scan
        bool many = (Nd > neighborhood_S_size + 1);
        bool found = false;
        NodeID z = 0;
        if (!many)
        {
          for (NodeID k = 0; k < Nd; k++)
          {
            NodeID w = n[k];
            if (extend_neighborhood_S.get(w)) // w is in N[S]
              continue;
            if (found)
            {
              many = true;
              break;
            }
            z = w;
            found = true;
          }
        }

        if (many)
          continue; // two or more vertices outside N[S] -> useless child, try the next
        if (!found)
        {
          v_unconfined = true; 
          break;
        }

        // Exactly one vertex outside N[S] -> extend S with it and keep scanning
        ok = false;
        add_to_S(z);
      }
    }

    // Diamond/satellite extension:
    // two vertices u_i, u_j that are NOT adjacent, each having exactly the same two
    // S-neighbours and no neighbour outside N[S], form a diamond that forces v out of
    // every maximum independent set.
    if (!v_unconfined && S_size >= 2)
    {
      ns_set.clear();
      for (NodeID i = 0; i < neighborhood_S_size; i++)
      {
        NodeID u = neighborhood_S[i];
        if (intersection_count[u] >= 1) 
          ns_set.add(u);
      }

      for (NodeID i = 0; i < neighborhood_S_size; i++)
      {
        vs1[i] = vs2[i] = VS_NONE;
        NodeID u = neighborhood_S[i];
        if (intersection_count[u] != 2)
          continue;

        NodeID Nd;
        NodeID *n = hypergraph_get_neighborhood(g, u, neighbors, Nd, mish_alg->node_set2);

        NodeID a = VS_NONE, b = VS_NONE;
        bool too_many = false;
        for (NodeID k = 0; k < Nd; k++)
        {
          NodeID w = n[k];
          if (ns_set.get(w))
            continue;
          if (a == VS_NONE)
            a = w;
          else if (b == VS_NONE)
            b = w;
          else
          {
            too_many = true;
            break;
          }
        }
        if (too_many || a == VS_NONE || b == VS_NONE)
          continue;

        if (a > b)
          std::swap(a, b);
        vs1[i] = a;
        vs2[i] = b;
      }

      for (NodeID i = 0; i < neighborhood_S_size && !v_unconfined; i++)
      {
        if (vs1[i] == VS_NONE)
          continue;

        NodeID u = neighborhood_S[i];
        NodeID Nd;
        NodeID *n = hypergraph_get_neighborhood(g, u, neighbors, Nd, mish_alg->node_set2);
        nu_set.clear();
        for (NodeID k = 0; k < Nd; k++)
          nu_set.add(n[k]);

        for (NodeID j = i + 1; j < neighborhood_S_size; j++)
        {
          if (vs1[j] != vs1[i] || vs2[j] != vs2[i])
            continue;
          if (nu_set.get(neighborhood_S[j])) // u_i and u_j adjacent -> no diamond
            continue;
          v_unconfined = true;
          break;
        }
      }
    }

    if (v_unconfined)
      mish_alg->set(v, IS_status::excluded);

    for (NodeID i = 0; i < neighborhood_S_size; i++)
      intersection_count[neighborhood_S[i]] = 0;
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

  // scale node_domination with the degree / neighborhood-size bounds by NODE_DOM_FACTOR
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
      if (deg_v == 0 || INCLUDE_DEG1)
        mish_alg->set(v, IS_status::included);
      continue;
    }

    for (NodeID i = 0; i < deg_v; i++)
    {
      NodeID u = n[i];
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
        { // n is the live g->N[v] (-f) -> it shrinks when u is removed
          i--;
          deg_v--;
        }
        else
        { // n is a buffer -> it does not shrink
          dominated++;
        }

        if (deg_v - dominated == 1)
        {
          if (INCLUDE_DEG1)
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
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - mish_alg->start_time).count();
    if (elapsed > TIME_KERNEL_SECONDS)
      break;

    NodeID v = marker.current_element(v_idx);

    if (status.node_status[v] != IS_status::not_set)
      continue;

    NodeID deg_v = g->Vd[v];
    if (deg_v > MAX_DEGREE)
      continue;

    // (The time check lives at the top of the loop now. It used to sit here and break
    // without carrying the unscanned tail over, silently retiring those vertices --
    // the same bug that cost unconfined most of its reduction.)

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

      // No early return: this used to bail out after the FIRST successful reduction and
      // hand back to the driver, which cycled all 7 rules before twin got back in --
      // and each re-entry restarts the scan at the front of the marker. Scanning the
      // whole marker in one go instead, like KaMIS's rules do; the time check at the
      // top of the loop bounds the work and carries its tail over.
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
