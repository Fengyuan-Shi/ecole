#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <xtensor-python/pytensor.hpp>

#include "ecole/observation/khalil-2016.hpp"
#include "ecole/observation/nodebipartite.hpp"
#include "ecole/observation/nothing.hpp"
#include "ecole/observation/pseudocosts.hpp"
#include "ecole/observation/strongbranchingscores.hpp"
#include "ecole/scip/model.hpp"
#include "ecole/utility/sparse-matrix.hpp"

#include "core.hpp"

namespace ecole::observation {

namespace py = pybind11;

/**
 * Helper function to bind the `before_reset` method of observation functions.
 */
template <typename PyClass, typename... Args> auto def_before_reset(PyClass pyclass, Args&&... args) {
	return pyclass.def(
		"before_reset",
		&PyClass::type::before_reset,
		py::arg("model"),
		py::call_guard<py::gil_scoped_release>(),
		std::forward<Args>(args)...);
}

/**
 * Helper function to bind the `extract` method of observation functions.
 */
template <typename PyClass, typename... Args> auto def_extract(PyClass pyclass, Args&&... args) {
	return pyclass.def(
		"extract",
		&PyClass::type::extract,
		py::arg("model"),
		py::arg("done"),
		py::call_guard<py::gil_scoped_release>(),
		std::forward<Args>(args)...);
}

/**
 * Observation module bindings definitions.
 */
void bind_submodule(py::module_ const& m) {
	m.doc() = "Observation classes for Ecole.";

	xt::import_numpy();

	m.attr("Nothing") = py::type::of<Nothing>();

	using coo_matrix = decltype(NodeBipartiteObs::edge_features);
	py::class_<coo_matrix>(m, "coo_matrix", R"(
		Sparse matrix in the coordinate format.

		Similar to Scipy's ``scipy.sparse.coo_matrix`` or PyTorch ``torch.sparse``.
	)")
		.def_property_readonly(
			"values", [](coo_matrix & self) -> auto& { return self.values; }, "A vector of non zero values in the matrix")
		.def_property_readonly(
			"indices",
			[](coo_matrix & self) -> auto& { return self.indices; },
			"A matrix holding the indices of non zero coefficient in the sparse matrix. "
			"There are as many columns as there are non zero coefficients, and each row is a "
			"dimension in the sparse matrix.")
		.def_property_readonly(
			"shape",
			[](coo_matrix& self) { return std::make_pair(self.shape[0], self.shape[1]); },
			"The dimension of the sparse matrix, as if it was dense.")
		.def_property_readonly("nnz", &coo_matrix::nnz);

	auto node_bipartite_obs = py::class_<NodeBipartiteObs>(m, "NodeBipartiteObs", R"(
		Bipartite graph observation for branch-and-bound nodes.

		The optimization problem is represented as an heterogenous bipartite graph.
		On one side, a node is associated with one variable, on the other side a node is
		associated with one constraint.
		There exist an edge between a variable and a constraint if the variable exists in the
		constraint with a non-zero coefficient.

		Each variable and constraint node is associated with a vector of features.
		Each edge is associated with the coefficient of the variable in the constraint.
	)");
	node_bipartite_obs
		.def_property_readonly(
			"column_features",
			[](NodeBipartiteObs & self) -> auto& { return self.column_features; },
			"A matrix where each row is represents a variable, and each column a feature of "
			"the variables.")
		.def_property_readonly(
			"row_features",
			[](NodeBipartiteObs & self) -> auto& { return self.row_features; },
			"A matrix where each row is represents a constraint, and each column a feature of "
			"the constraints.")
		.def_readwrite(
			"edge_features",
			&NodeBipartiteObs::edge_features,
			"The constraint matrix of the optimization problem, with rows for contraints and "
			"columns for variables.");

	py::enum_<NodeBipartiteObs::ColumnFeatures>(node_bipartite_obs, "ColumnFeatures")
		.value("has_lower_bound", NodeBipartiteObs::ColumnFeatures::has_lower_bound)
		.value("has_upper_bound", NodeBipartiteObs::ColumnFeatures::has_upper_bound)
		.value("normed_reduced_cost", NodeBipartiteObs::ColumnFeatures::normed_reduced_cost)
		.value("objective", NodeBipartiteObs::ColumnFeatures::objective)
		.value("solution_value", NodeBipartiteObs::ColumnFeatures::solution_value)
		.value("solution_frac", NodeBipartiteObs::ColumnFeatures::solution_frac)
		.value("is_solution_at_lower_bound", NodeBipartiteObs::ColumnFeatures::is_solution_at_lower_bound)
		.value("is_solution_at_upper_bound", NodeBipartiteObs::ColumnFeatures::is_solution_at_upper_bound)
		.value("scaled_age", NodeBipartiteObs::ColumnFeatures::scaled_age)
		.value("is_basis_lower", NodeBipartiteObs::ColumnFeatures::is_basis_lower)
		.value("is_basis_basic", NodeBipartiteObs::ColumnFeatures::is_basis_basic)
		.value("is_basis_upper", NodeBipartiteObs::ColumnFeatures::is_basis_upper)
		.value("is_basis_zero", NodeBipartiteObs::ColumnFeatures ::is_basis_zero)
		.value("incumbent_value", NodeBipartiteObs::ColumnFeatures::incumbent_value)
		.value("average_incumbent_value", NodeBipartiteObs::ColumnFeatures::average_incumbent_value)
		.value("is_type_binary", NodeBipartiteObs::ColumnFeatures::is_type_binary)
		.value("is_type_integer", NodeBipartiteObs::ColumnFeatures::is_type_integer)
		.value("is_type_implicit_integer", NodeBipartiteObs::ColumnFeatures::is_type_implicit_integer)
		.value("is_type_continuous", NodeBipartiteObs::ColumnFeatures::is_type_continuous);

	py::enum_<NodeBipartiteObs::RowFeatures>(node_bipartite_obs, "RowFeatures")
		.value("bias", NodeBipartiteObs::RowFeatures::bias)
		.value("is_tight", NodeBipartiteObs::RowFeatures::is_tight)
		.value("scaled_age", NodeBipartiteObs::RowFeatures::scaled_age)
		.value("objective_cosine_similarity", NodeBipartiteObs::RowFeatures::objective_cosine_similarity)
		.value("dual_solution_value", NodeBipartiteObs::RowFeatures::dual_solution_value);

	auto node_bipartite = py::class_<NodeBipartite>(m, "NodeBipartite", R"(
		Bipartite graph observation function on branch-and bound node.

		This observation function extract structured :py:class:`NodeBipartiteObs`.
	)");
	node_bipartite.def(py::init<>());
	def_before_reset(node_bipartite, "Cache some feature not expected to change during an episode.");
	def_extract(node_bipartite, "Extract a new :py:class:`NodeBipartiteObs`.");

	auto strong_branching_scores = py::class_<StrongBranchingScores>(m, "StrongBranchingScores", R"(
		Strong branching score observation function on branch-and bound node.

		This observation obtains scores for all LP or pseudo candidate variables at a
		branch-and-bound node.  The strong branching score measures the quality of branching
		for each variable.  This observation can be used as an expert for imitation
		learning algorithms.

		This observation function extracts an array containing the strong branching score for
		each variable in the problem which can be indexed by the action set.  Variables for which
		a strong branching score is not applicable are filled with NaN.
	)");
	strong_branching_scores.def(py::init<bool>(), py::arg("pseudo_candidates") = true, R"(
		Constructor for StrongBranchingScores.

		Parameters
		----------
		pseudo_candidates :
			The parameter determines if strong branching scores are computed for
			psuedo-candidate variables if true or LP canidate variables if false.
			By default psuedo-candidates will be computed.
	)");
	def_before_reset(strong_branching_scores, R"(Do nothing.)");
	def_extract(strong_branching_scores, "Extract an array containing strong branching scores.");

	auto pseudocosts = py::class_<Pseudocosts>(m, "Pseudocosts", R"(
		Pseudocosts observation function on branch-and bound node.

		This observation obtains pseudocosts for all LP fractional candidate variables at a
		branch-and-bound node.  The pseudocost is a cheap approximation to the strong branching
		score and measures the quality of branching for each variable.  This observation can be used
		as a practical branching strategy by always branching on the variable with the highest
		pseudocost, although in practice is it not as efficient as SCIP's default strategy, reliability
		pseudocost branching (also known as hybrid branching).

		This observation function extracts an array containing the pseudocost for
		each variable in the problem which can be indexed by the action set.  Variables for which
		a pseudocost is not applicable are filled with NaN.
	)");
	pseudocosts.def(py::init<>());
	def_before_reset(pseudocosts, R"(Do nothing.)");
	def_extract(pseudocosts, "Extract an array containing pseudocosts.");

	auto khalil_2016 = py::class_<Khalil2016>(m, "Khalil2016", R"(
		Branching candidates features from Khalil et al. (2016).

		The observation is a matrix where rows represent pseudo branching cnadidates and columns
		represent features related to these variables.
		See [Khalil2016]_ for a complete reference on this objservation function.

		.. [Khalil2016]
			Khalil, Elias Boutros, Pierre Le Bodic, Le Song, George Nemhauser, and Bistra Dilkina.
			"`Learning to branch in mixed integer programming.
			<https://www.cc.gatech.edu/~lsong/papers/KhaLebSonNemDil16.pdf>`_"
			*Thirtieth AAAI Conference on Artificial Intelligence*. 2016.
	)");
	khalil_2016.def(py::init<>());
	def_before_reset(khalil_2016, R"(Precompute static features for all varaible columns.)");
	def_extract(khalil_2016, "Extract the observation matrix.");
}

}  // namespace ecole::observation
