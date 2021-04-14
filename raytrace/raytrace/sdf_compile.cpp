#include <string>
#include <vector>
#include <iostream>
#include <cassert>
#include <sstream>
#include <map>

#include "sdf_compile.h"

enum class symtype {
	OPEN_PAREN,
	CLOSE_PAREN,
	LITERAL,
	OPERATOR,
};

struct symbol {
	symtype type_;
	std::string content_;
};

bool is_digit(char c) {
	return c >= '0' && c <= '9';
}

bool is_numerical(const std::string& c) {
	return is_digit(c[0]) || (c[0] == '-' && is_digit(c[1]));
}

// Reads a scene string and returns the equivalent vector of logical symbols to be parsed.
std::vector<symbol> symbolize(const std::string& str) {
	std::vector<symbol> symbols;
	std::string current_symbol;
	auto it = str.begin();
	while (it < str.end()) {
		char c = *it;
		bool is_whitespace = (c == ' ' || c == '\n' || c == '\t');
		if (current_symbol.size() > 0 && (c == '(' || c == ')' || is_whitespace)) {
			symbols.push_back({ is_numerical(current_symbol) ? symtype::LITERAL : symtype::OPERATOR, current_symbol });
			current_symbol = "";
		}

		if (c == '(') {
			symbols.push_back({ symtype::OPEN_PAREN });
		}
		else if (c == ')') {
			symbols.push_back({ symtype::CLOSE_PAREN });
		}
		else if (!is_whitespace) {
			current_symbol += c;
		}

		++it;
	}

	return symbols;
}

float parse_f(const std::string& s) {
	return ::atof(s.c_str());
}

struct expr {
	bool literal_;
	// Set the the value of the literal if |literal_| is true
	float lit_value_;
	// Set to the operator string if |literal_| is false
	std::string operator_;
	// The operands of |operator_| if |literal_| is false
	std::vector<expr> operands_;
};

// Parse the symbol list into an expression tree that can be evaluated.
// Returns the root of the tree.
expr parse(std::vector<symbol>::iterator* it) {
	switch ((*it)->type_)
	{
	case symtype::LITERAL: {
		expr ret = { true, parse_f((*it)->content_) };
		++(*it);
		return ret;
		break;
	}
	case symtype::OPEN_PAREN: {
		++(*it);
		if ((*it)->type_ != symtype::OPERATOR) std::cout << "Open paren not followed by operator" << std::endl;
		expr ret = { false, 0.f, (*it)->content_ };
		++(*it);

		while ((*it)->type_ != symtype::CLOSE_PAREN) {
			ret.operands_.push_back(parse(it));
		}
		++(*it);

		return ret;
		break;
	}
	default:
		std::cerr << "Unknown symbol " << (int)(*it)->type_ << " " << (*it)->content_ << std::endl;
		break;
	}
}

void print_tree(const expr& node, int level = 0) {
	for (int i = 0; i < level; ++i) {
		std::cout << " ";
	}

	if (node.literal_) {
		std::cout << node.lit_value_ << std::endl;
	}
	else {
		std::cout << node.operator_ << std::endl;
		for (const auto& c : node.operands_) {
			print_tree(c, level + 1);
		}
	}
}

std::map<std::string, std::string> transforms_ {
	{ "tTrans", "rtrans" },
	{ "tRot", "rrot" }
};

std::map<std::string, std::string> specials_ {
	{ ".time", "elapsed_time" },
	{ ".tp", "" },
	{ ".p", "p" },
};

std::map<std::string, std::string> mat_macros_ {
	{ "mLamb", "MAKE_LAMB" },
	{ "mDiel", "MAKE_DIEL" },
	{ "mEmit", "MAKE_EMIT" },
};

std::map<std::string, int> prim_args_ {
	{ "pSphere", 1 },
	{ "pBox", 3 },
	{ "pPlane", 4},
	{ "pOctahedron", 1},
};

std::map<std::string, std::string> prims_ {
	{ "pSphere", "sphere_sdf" },
	{ "pBox", "box_sdf" },
	{ "pPlane", "plane_sdf" },
	{ "pOctahedron", "octahedron_bound_sdf" },
};

std::map<std::string, std::string> ops_{
	{ "oUnion", "union_sdf" },
	{ "oSub", "sub_sdf" },
	{ "oNeg", "neg_sdf" },
	{ "oSmoothUnion", "smooth_union_sdf" },
	{ "oFbm", "fbm_noise" },
};
std::map<std::string, int> ops_literal_args_{
	{ "oUnion", 0 },
	{ "oSub", 0 },
	{ "oNeg", 0 },
	{ "oSmoothUnion", 1 },
	{ "oFbm", 2 },
};

// This function recursively traverses |root| and generates the statements for the individual SDF
// leaf primitives (the actual volumes making up the SDF). It'll emit a series of statements of the form
//
// sdf_result_t p0 = sdf_result_t(sphere_sdf(rrot(axis, angle, p), 1.0), MAKE_DIEL(1.5));
//
// Which represents a 1.0-radius sphere rotated |angle| degrees around |axis|, which a dielectric
// material of refraction index 1.5
std::string eval_primitives(expr root, std::string current_mat, std::string current_p, int* primitive_count) {
	if (root.literal_) {
		std::stringstream ss;
		ss << root.lit_value_;
		return ss.str();
	}

	if (root.operator_.rfind("p", 0) == 0) {
		std::stringstream ss;
		int prim_idx = (*primitive_count)++;
		ss << "sdf_result_t p" << prim_idx << " = sdf_result_t(";
		assert(prim_args_[root.operator_] == root.operands_.size());
		ss << prims_[root.operator_] << "(" << current_p << ", ";
		for (int i = 0; i < root.operands_.size(); ++i) {
			ss << eval_primitives(root.operands_[i], current_mat, current_p, primitive_count);
			if (i != root.operands_.size() - 1) {
				ss << ", ";
			}
		}
		ss << "), " << current_mat << ");" << std::endl;
		return ss.str();
	}

	if (root.operator_.rfind("o", 0) == 0) {
		std::stringstream ss;
		int lit_args_c = ops_literal_args_[root.operator_]; // Skip literal arguments for operators in this pass
		for (int i = lit_args_c; i < root.operands_.size(); ++i) {
			ss << eval_primitives(root.operands_[i], current_mat, current_p, primitive_count);
		}
		return ss.str();
	}

	if (root.operator_.rfind("m", 0) == 0) {
		std::stringstream ss;
		ss << mat_macros_[root.operator_] << "(";
		for (int i = 0; i < root.operands_.size() - 1; ++i) {
			ss << eval_primitives(root.operands_[i], current_mat, current_p, primitive_count);
			if (i < root.operands_.size() - 2) {
				ss << ", ";
			}
		}
		ss << ")";
		return eval_primitives(root.operands_[root.operands_.size() - 1], ss.str(), current_p, primitive_count);
	}

	if (root.operator_.rfind(".", 0) == 0) {
		if (root.operator_ == ".tp") return current_p;
		return specials_[root.operator_];
	}

	if (root.operator_.rfind("t", 0) == 0) {
		std::stringstream ss;

		ss << transforms_[root.operator_] << "(" << current_p << ", ";
		for (int i = 0; i < root.operands_.size() - 1; ++i) {
			ss << eval_primitives(root.operands_[i], current_mat, current_p, primitive_count);
			if (i < root.operands_.size() - 2) {
				ss << ", ";
			}
		}
		ss << ")";

		return eval_primitives(root.operands_[root.operands_.size() - 1], current_mat, ss.str(), primitive_count);
	}

	assert(false);
	return "";
}

// This function is similar to eval_primitives(), except it generates the evaluation statement for
// the entire SDF, using the variables declared by eval_primitives(). It *could* just inline the
// SDF evaluation code instead of splitting it in 2 functions, but 1/ this is a remnant of much
// messier code and 2/ there might come a time when this split is useful (maybe when volumetric
// transport or importance sampling towards lights are supported).
std::string eval_concat(expr root, std::string current_p, int* primitive_count) {
	if (root.literal_) {
		std::stringstream ss;
		ss << root.lit_value_;
		return ss.str();
	}

	if (root.operator_.rfind("p", 0) == 0) {
		// If this is a primitive, we've already processed it and added it to the preamble, so we just need to return the variable name.
		std::stringstream ss;
		ss << "p" << (*primitive_count)++;
		return ss.str();
	}

	if (root.operator_.rfind("o", 0) == 0) {
		std::stringstream lit_args_ss;
		int lit_args_c = ops_literal_args_[root.operator_];
		for (int i = 0; i < lit_args_c; ++i) {
			lit_args_ss << eval_concat(root.operands_[i], current_p, primitive_count);
			lit_args_ss << ", ";
		}
		std::string lit_args = lit_args_ss.str();

		std::string left = eval_concat(root.operands_[lit_args_c], current_p, primitive_count);
		if (root.operands_.size() - lit_args_c == 1) {
			std::stringstream ss;
			ss << ops_[root.operator_] << "(" << lit_args << left << ")";
			return ss.str();
		}

		for (int i = lit_args_c + 1; i < root.operands_.size(); ++i) {
			std::stringstream ss;
			std::string right = eval_concat(root.operands_[i], current_p, primitive_count);
			ss << ops_[root.operator_] << "(" << lit_args << left << ", " << right << ")";
			left = ss.str();
		}

		return left;
	}

	if (root.operator_.rfind(".", 0) == 0) {
		if (root.operator_ == ".tp") return current_p;
		return specials_[root.operator_];
	}

	if (root.operator_.rfind("t", 0) == 0) {
		std::stringstream ss;

		ss << transforms_[root.operator_] << "(" << current_p << ", ";
		for (int i = 0; i < root.operands_.size() - 1; ++i) {
			ss << eval_concat(root.operands_[i], current_p, primitive_count);
			if (i < root.operands_.size() - 2) {
				ss << ", ";
			}
		}
		ss << ")";

		return eval_concat(root.operands_[root.operands_.size() - 1], ss.str(), primitive_count);
	}

	if (root.operator_.rfind("m", 0) == 0 || root.operator_.rfind("t", 0) == 0) {
		return eval_concat(root.operands_[root.operands_.size() - 1], current_p, primitive_count);
	}

	return "";
}

std::string compile_scene(const std::string& scene) {
	std::stringstream r;

	auto s = symbolize(scene);

	expr root = parse(&s.begin());

	int primitive_count = 0;
	r << eval_primitives(root, "MAKE_NO_MAT()", "p", &primitive_count) << std::endl;

	primitive_count = 0;
	std::stringstream concat_;
	concat_ << "return sdf_result_t(";
	concat_ << eval_concat(root, "p", &primitive_count);
	concat_ << ");";

	r << concat_.str() << std::endl;

	return r.str();
}