#pragma once

// local includes
#include "definitions.h"
#include "fast_set.h"
#include "hypergraph.h"
#include "config.h"

// system includes
#include <vector>
#include <memory>
#include <array>
#include <string>

class MISH_algorithm;

enum reduction_type
{
	edge_degree_one,
	node_degree_one,
	sunflower,
	node_domination,
	twin,
	unconfined,
	edge_domination
};

template <typename NodeID>
class element_marker
{
private:
	std::vector<NodeID> current;
	std::vector<NodeID> next;
	fast_set added_elements;

public:
	element_marker(size_t size) : added_elements(size)
	{
		current.reserve(size);
		next.reserve(size);
	}

	void add(NodeID vertex)
	{
		if (!added_elements.get(vertex))
		{
			next.push_back(vertex);
			added_elements.add(vertex);
		}
	}

	void get_next()
	{
		current.swap(next);
		clear_next();
	}

	void clear_next()
	{
		next.clear();
		added_elements.clear();
	}

	void fill_current_ascending(size_t n)
	{
		current.clear();
		for (size_t i = 0; i < n; i++)
		{
			current.push_back(i);
		}
	}

	NodeID current_element(size_t index)
	{
		return current[index];
	}

	size_t current_size()
	{
		return current.size();
	}
};

struct general_reduction
{
	general_reduction(size_t n) : marker(n) {}
	virtual ~general_reduction() {}
	virtual general_reduction *clone() const = 0;

	virtual reduction_type get_reduction_type() const = 0;
	virtual std::string get_reduction_name() const = 0;
	virtual bool reduce(MISH_algorithm *mish_alg) = 0;
	virtual void restore(MISH_algorithm *mish_alg) {}
	virtual void apply(MISH_algorithm *mish_alg) {}

	bool has_run = false;
	bool vertex_rule = true;
	element_marker<NodeID> marker;
};

struct edge_degree_one_reduction : public general_reduction
{
	edge_degree_one_reduction(size_t n, size_t m) : general_reduction(m)
	{
		vertex_rule = false;
	}
	~edge_degree_one_reduction() {}
	virtual edge_degree_one_reduction *clone() const final { return new edge_degree_one_reduction(*this); }

	virtual reduction_type get_reduction_type() const final { return reduction_type::edge_degree_one; }
	virtual std::string get_reduction_name() const final { return "edge_degree_one"; }
	virtual bool reduce(MISH_algorithm *mish_alg) final;
};

struct node_degree_one_reduction : public general_reduction
{
	node_degree_one_reduction(size_t n, size_t m) : general_reduction(n)
	{
	}
	~node_degree_one_reduction() {}
	virtual node_degree_one_reduction *clone() const final { return new node_degree_one_reduction(*this); }

	virtual reduction_type get_reduction_type() const final { return reduction_type::node_degree_one; }
	virtual std::string get_reduction_name() const final { return "node_degree_one"; }
	virtual bool reduce(MISH_algorithm *mish_alg) final;
};

struct sunflower_reduction : public general_reduction
{
	sunflower_reduction(size_t n,size_t m) : general_reduction(n)
	{
	}
	~sunflower_reduction() {}
	virtual sunflower_reduction *clone() const final { return new sunflower_reduction(*this); }

	virtual reduction_type get_reduction_type() const final { return reduction_type::sunflower; }
	virtual std::string get_reduction_name() const final { return "sunflower"; }
	virtual bool reduce(MISH_algorithm *mish_alg) final;
};

struct node_domination_reduction : public general_reduction
{
	node_domination_reduction(size_t n, size_t m) : general_reduction(n)
	{
	}
	~node_domination_reduction() {}
	virtual node_domination_reduction *clone() const final { return new node_domination_reduction(*this); }

	virtual reduction_type get_reduction_type() const final { return reduction_type::node_domination; }
	virtual std::string get_reduction_name() const final { return "node_domination"; }
	virtual bool reduce(MISH_algorithm *mish_alg) final;
};

struct twin_reduction : public general_reduction
{
	twin_reduction(size_t n, size_t m) : general_reduction(n)
	{
	}
	~twin_reduction() {}
	virtual twin_reduction *clone() const final { return new twin_reduction(*this); }

	virtual reduction_type get_reduction_type() const final { return reduction_type::twin; }
	virtual std::string get_reduction_name() const final { return "twin"; }
	virtual bool reduce(MISH_algorithm *mish_alg) final;

	struct Twin
	{
		std::vector<NodeID> twin_nodes;
		size_t min_degree;
	};
};

struct unconfined_reduction : public general_reduction
{
	unconfined_reduction(size_t n, size_t m) : general_reduction(n)
	{
	}
	~unconfined_reduction() {}
	virtual unconfined_reduction *clone() const final { return new unconfined_reduction(*this); }

	virtual reduction_type get_reduction_type() const final { return reduction_type::unconfined; }
	virtual std::string get_reduction_name() const final { return "unconfined"; }
	virtual bool reduce(MISH_algorithm *mish_alg) final;
};

struct edge_domination_reduction : public general_reduction
{
	edge_domination_reduction(size_t n, size_t m) : general_reduction(m)
	{
		vertex_rule = false;
	}
	~edge_domination_reduction() {}
	virtual edge_domination_reduction *clone() const final { return new edge_domination_reduction(*this); }

	virtual reduction_type get_reduction_type() const final { return reduction_type::edge_domination; }
	virtual std::string get_reduction_name() const final { return "edge_domination"; }
	virtual bool reduce(MISH_algorithm *mish_alg) final;
};

struct reduction_ptr
{
	general_reduction *reduction = nullptr;

	reduction_ptr() = default;

	~reduction_ptr()
	{
		release();
	}

	reduction_ptr(general_reduction *reduction) : reduction(reduction) {}

	reduction_ptr(const reduction_ptr &other) : reduction(other.reduction->clone()) {}

	reduction_ptr &operator=(const reduction_ptr &other)
	{
		release();
		reduction = other.reduction->clone();
		return *this;
	}

	reduction_ptr(reduction_ptr &&other) : reduction(std::move(other.reduction))
	{
		other.reduction = nullptr;
	}

	reduction_ptr &operator=(reduction_ptr &&other)
	{
		reduction = std::move(other.reduction);
		other.reduction = nullptr;
		return *this;
	}

	general_reduction *operator->() const
	{
		return reduction;
	}

	void release()
	{
		if (reduction)
		{
			delete reduction;
			reduction = nullptr;
		}
	}
};

template <class Last>
void make_reduction_vector_helper(std::vector<reduction_ptr> &vec, size_t n, size_t m)
{
	vec.emplace_back(new Last(n,m));
};

template <class First, class Second, class... Redus>
void make_reduction_vector_helper(std::vector<reduction_ptr> &vec, size_t n, size_t m)
{
	vec.emplace_back(new First(n,m));
	make_reduction_vector_helper<Second, Redus...>(vec, n, m);
};

template <class... Redus>
std::vector<reduction_ptr> make_reduction_vector(size_t n, size_t m)
{
	std::vector<reduction_ptr> vec;
	vec.reserve(10);
	make_reduction_vector_helper<Redus...>(vec, n, m);
	return vec;
};

void clear_reduction_vector(std::vector<reduction_ptr> &vec);

// Test if A and B are equal
static inline int set_is_equal(const NodeID *A, NodeID a, const NodeID *B, NodeID b);
// Test if A is a subset of B
static inline int set_is_subset(const NodeID *A, NodeID a, const NodeID *B, NodeID b);
// Test if A is a subset of B, ignoring x from A
static inline int set_is_subset_except_one(const NodeID *A, NodeID a, const NodeID *B, NodeID b, NodeID x);
// Test if A is a subset of B, ignoring x and negative numbers from A
static inline int set_is_subset_except_one_positive(const NodeID *A, NodeID a, const NodeID *B, NodeID b, NodeID x);
// Test if A \ B = \emtpyset (return -1) if A\B = {v} (return v) else return -2
static inline int set_difference_check(const NodeID *A, NodeID a, const NodeID *B, NodeID b);
// Test if |A \cap B| == 1
static inline int set_intersection_equal_one(const NodeID *A, NodeID a, const NodeID *B, NodeID b);