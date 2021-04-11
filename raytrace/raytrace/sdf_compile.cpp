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

std::vector<symbol> symbolize(const std::string& str) {
	std::vector<symbol> symbols;
	std::string current_symbol;
	auto it = str.begin();
	while (it < str.end()) {
		char c = *it;
		if (current_symbol.size() > 0 && (c == '(' || c == ')' || c == ' ')) {
			symbols.push_back({ is_numerical(current_symbol) ? symtype::LITERAL : symtype::OPERATOR, current_symbol });
			current_symbol = "";
		}

		if (c == '(') {
			symbols.push_back({ symtype::OPEN_PAREN });
		}
		else if (c == ')') {
			symbols.push_back({ symtype::CLOSE_PAREN });
		}
		else if (c != ' ') {
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
	float lit_value_;
	std::string operator_;
	std::vector<expr> operands_;
};

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

std::stringstream primitives_;
std::stringstream materials_;
std::stringstream concat_;

int primitive_count_ = 0;

std::map<std::string, std::string> mat_macros_{
	{ "mLamb", "MAKE_LAMB" },
	{ "mDiel", "MAKE_DIEL" },
};
std::map<std::string, int> prim_args_{
	{ "pSphere", 1 },
	{ "pBox", 3 },
};

std::map<std::string, std::string> prims_{
	{ "pSphere", "sphere_sdf" },
	{ "pBox", "box_sdf" },
};
std::string eval_primitives(expr root, std::string current_mat) {
	if (root.literal_) {
		std::stringstream ss;
		ss << root.lit_value_;
		return ss.str();
	}

	if (root.operator_.rfind("p", 0) == 0) {
		std::stringstream ss;
		int prim_idx = primitive_count_++;
		ss << "sdf_result_t p" << prim_idx << " = sdf_result_t(";
		assert(prim_args_[root.operator_] == root.operands_.size());
		ss << prims_[root.operator_] << "(p, ";
		for (int i = 0; i < root.operands_.size(); ++i) {
			ss << eval_primitives(root.operands_[i], current_mat);
			if (i != root.operands_.size() - 1) {
				ss << ", ";
			}
		}
		ss << "), " << current_mat << ");" << std::endl;
		materials_ << "if (abs(p" << prim_idx << ".dist) < abs(closest.dist)) closest = p" << prim_idx << ";" << std::endl;
		return ss.str();
	}

	if (root.operator_.rfind("o", 0) == 0) {
		std::stringstream ss;
		for (const auto& c : root.operands_) {
			ss << eval_primitives(c, current_mat);
		}
		return ss.str();
	}

	if (root.operator_.rfind("m", 0) == 0) {
		std::stringstream ss;
		ss << mat_macros_[root.operator_] << "(";
		for (int i = 0; i < root.operands_.size() - 1; ++i) {
			ss << eval_primitives(root.operands_[i], current_mat);
			if (i < root.operands_.size() - 2) {
				ss << ", ";
			}
		}
		ss << ")";
		return eval_primitives(root.operands_[root.operands_.size() - 1], ss.str());
	}

	assert(false);
	return "";
}

std::map<std::string, std::string> ops_{
	{ "oUnion", "union_sdf" },
	{ "oSub", "sub_sdf" },
	{ "oNeg", "neg_sdf" },
};
int concat_prim_count_ = 0;
std::string eval_concat(expr root) {
	assert(!root.literal_);

	if (root.operator_.rfind("p", 0) == 0) {
		// If this is a primitive, we've already processed it and added it to the preamble, so we just need to return the variable name.
		std::stringstream ss;
		ss << "p" << concat_prim_count_++ << ".dist";
		return ss.str();
	}

	if (root.operator_.rfind("o", 0) == 0) {
		std::string left = eval_concat(root.operands_[0]);
		if (root.operands_.size() == 1) {
			std::stringstream ss;
			ss << ops_[root.operator_] << "(" << left << ")";
			left = ss.str();
		}
		else {
			for (int i = 1; i < root.operands_.size(); ++i) {
				std::stringstream ss;
				std::string right = eval_concat(root.operands_[i]);
				ss << ops_[root.operator_] << "(" << left << ", " << right << ")";
				left = ss.str();
			}
		}
		return left;
	}

	if (root.operator_.rfind("m", 0) == 0) {
		return eval_concat(root.operands_[root.operands_.size() - 1]);
	}

	return "";
}

std::string compile_scene(const std::string& scene) {
	std::stringstream r;
	// std::string scene = "(oUnion (oNeg (mLamb 12 13 14 (pSphere 1))) (mLamb 15 16 17 (oUnion (pBox 2 3 4) (pBox 5 6 7))) (oSub (mLamb 18 19 20 (pSphere 8)) (pBox 9 10 11)))";

	auto s = symbolize(scene);
	std::cout << "Num symbols: " << s.size() << std::endl;

	expr root = parse(&s.begin());
	print_tree(root);
	std::cout << std::endl;

	materials_ << "sdf_result_t closest = sdf_result_t(kTMax, MAKE_NO_MAT());" << std::endl;
	r << eval_primitives(root, "MAKE_NO_MAT()") << std::endl;

	materials_ << std::endl << "mat_t mat = MAKE_NO_MAT();" << std::endl;
	materials_ << "if (abs(closest.dist) <= kEpsilon) mat = closest.closest_material;" << std::endl;

	r << materials_.str() << std::endl;

	concat_ << "return sdf_result_t(";
	concat_ << eval_concat(root);
	concat_ << ", mat);" << std::endl;

	r << concat_.str() << std::endl;

	return r.str();
}