
#include "gurobi_c++.h"
#include "ILP_solver.h"
#include <queue>
#include <random>

std::pair<NodeID, int> ILP_solver(hypergraph *g, double time_limit_seconds, std::chrono::_V2::system_clock::time_point start_time, std::vector<bool> &solution)
{
    // idea: check degree 1 nodes that are in the MIS for the heuristic, and fix them in ILP
    if (g->n == 0)
    {
        return {0, 0.0};
    }
    try
    {
        GRBEnv env = GRBEnv(true);
        env.set(GRB_IntParam_OutputFlag, 0);
        env.start();

        env.set(GRB_IntParam_Threads, 1);

        GRBModel model = GRBModel(env);

        // create variables
        std::vector<double> lb(g->n, 0.0);
        std::vector<double> ub(g->n, 1.0);
        std::vector<double> obj(g->n, 1.0);
        std::vector<char> vtype(g->n, GRB_BINARY);
        GRBVar *x_ptr = model.addVars(lb.data(), ub.data(), obj.data(), vtype.data(), nullptr, g->n);
        std::vector<GRBVar> x(x_ptr, x_ptr + g->n);

        std::vector<double> coeffs(g->n, 1.0);
        GRBLinExpr objective;
        objective.addTerms(coeffs.data(), x.data(), g->n);
        model.setObjective(objective, GRB_MAXIMIZE);

        for (NodeID e = 0; e < g->m; e++)
        {
            if (g->Ed[e] < 2)
                continue;

            // Build arrays for this edge
            std::vector<GRBVar> cvars;
            cvars.reserve(g->n);
            std::vector<double> ccoeffs(g->Ed[e], 1.0);

            for (NodeID i = 0; i < g->Ed[e]; i++)
            {
                NodeID v = g->E[e][i];
                cvars.push_back(x[v]);
            }

            // Build expression in bulk
            GRBLinExpr edge_constraint;
            edge_constraint.addTerms(ccoeffs.data(), cvars.data(), g->Ed[e]);

            model.addConstr(edge_constraint <= 1);
        }

        // MIPGap
        model.set(GRB_DoubleParam_MIPGap, 0.0);
        std::chrono::duration<double> time_passed = std::chrono::high_resolution_clock::now() - start_time;
        // timelimit
        model.set(GRB_DoubleParam_TimeLimit, time_limit_seconds - time_passed.count());

        model.optimize();

        NodeID res = 0;

        for (NodeID i = 0; i < g->n; ++i)
        {
            if (x[i].get(GRB_DoubleAttr_X) > 0.5)
            {
                solution[i] = 1;
                res++;
            }
        }

        int optimstatus = model.get(GRB_IntAttr_Status);

        return {res, optimstatus};
    }
        catch (GRBException& e)
        {
        std::cerr << "Gurobi Exception: " << e.getMessage() << std::endl;
    }
    catch (...)
    {
        std::cerr << "Unknown error while solving the ILP occured." << std::endl;
    }
    return {-1, -1.0};
}

std::pair<NodeID, int> ILP_solver_graphs(graph *g, double time_limit_seconds, std::chrono::_V2::system_clock::time_point start_time, std::vector<bool> &solution)
{
    if (g->n == 0)
    {
        return {0, 0.0};
    }
    try
    {
        GRBEnv env = GRBEnv(true);
        env.set(GRB_IntParam_OutputFlag, 0);
        env.start();

        env.set(GRB_IntParam_Threads, 1);

        GRBModel model = GRBModel(env);

        // create variables
        std::vector<double> lb(g->n, 0.0);
        std::vector<double> ub(g->n, 1.0);
        std::vector<double> obj(g->n, 1.0);
        std::vector<char> vtype(g->n, GRB_BINARY);
        GRBVar *x_ptr = model.addVars(lb.data(), ub.data(), obj.data(), vtype.data(), nullptr, g->n);
        std::vector<GRBVar> x(x_ptr, x_ptr + g->n);

        std::vector<double> coeffs(g->n, 1.0);
        GRBLinExpr objective;
        objective.addTerms(coeffs.data(), x.data(), g->n);
        model.setObjective(objective, GRB_MAXIMIZE);

        // Add constraint x_u + x_v <= 1 for every graph edge (avoid duplicates by requiring v > u)
        for (NodeID u = 0; u < g->n; ++u)
        {
            for (NodeID e = g->V[u]; e < g->V[u + 1]; ++e)
            {
                NodeID v = g->E[e];
                if (v <= u) // skip to avoid adding the same undirected edge twice
                    continue;

                GRBLinExpr edge_constraint = x[u] + x[v];
                model.addConstr(edge_constraint <= 1);
            }
        }

        // MIPGap
        model.set(GRB_DoubleParam_MIPGap, 0.0);
        std::chrono::duration<double> time_passed = std::chrono::high_resolution_clock::now() - start_time;
        // timelimit
        model.set(GRB_DoubleParam_TimeLimit, time_limit_seconds - time_passed.count());

        model.optimize();

        NodeID res = 0;

        for (NodeID i = 0; i < g->n; ++i)
        {
            if (x[i].get(GRB_DoubleAttr_X) > 0.5)
            {
                solution[i] = 1;
                res++;
            }
        }

        int optimstatus = model.get(GRB_IntAttr_Status);

        return {res, optimstatus};
    }
    catch (GRBException e)
    {
        std::cerr << "Gurobi Exception: " << e.getMessage() << std::endl;
    }
    catch (...)
    {
        std::cerr << "Unknown error while solving the ILP occured." << std::endl;
    }
    return {-1, -1.0};
}

bool verifier(hypergraph *g, std::vector<bool> &IS)
{
    int included_nodes = 0;
    for (NodeID node = 0; node < g->n; node++)
    {
        if (IS[node])
        {
            for (NodeID i = 0; i < g->Nd[node]; i++)
            {
                NodeID neighbor = g->N[node][i];
                if (IS[neighbor])
                {
                    std::cout << "ERROR: node " << node << " and its neighbor " << neighbor << " are both included in the IS!!!" << std::endl;
                    return false;
                }
            }
            included_nodes++;
        }
    }
    return true;
}

bool verifierMIS(MISH_algorithm *mis_alg, hypergraph *g)
{
    auto &status = mis_alg->status;
    int included_nodes = 0;
    int excluded_nodes = 0;
    for (NodeID v = 0; v < g->n; v++)
    {
        if (status.node_status[v] == IS_status::included)
        {
            included_nodes++;
            for (NodeID i = 0; i < g->Nd[v]; i++)
            {
                NodeID u = g->N[v][i];
                if (status.node_status[u] == IS_status::included)
                {
                    std::cout << "ERROR: node " << v << " and its neighbor " << u << " are both included in the IS!!!" << std::endl;
                    return false;
                }
            }
        }
        else if (status.node_status[v] == IS_status::excluded)
        {
            excluded_nodes++;
        }
    }

    if (included_nodes != status.IS_size)
    {
        std::cout << "ERROR: included nodes and IS_size NOT the same" << std::endl;
        return false;
    }
    if ((included_nodes + excluded_nodes) > g->n)
    {
        std::cout << "ERROR: included and excluded nodes DONT add up!" << std::endl;
        return false;
    }
    return true;
}