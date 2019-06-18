#include <hdlConvertor/verilogPreproc/macro_def.h>
#include <cctype>
#include <algorithm>
#include <sstream>
#include <assert.h>

using namespace std;

namespace hdlConvertor {
namespace verilog_pp {

MacroDef::MacroDef(const string & _name, bool _has_params,
		const vector<string> & _params,
		const std::map<std::string, std::string> & _default_args,
		const string & _body) :
		name(_name), has_params(_has_params), params(_params), default_args(
				_default_args) {
	parse_body(_params, _body, body);
}

void MacroDef::parse_body(const std::vector<std::string> & params,
		const std::string & body, std::vector<MacroDef::Fragment> & res) {
	collect_string_intervals(body);
	bool no_body = body.size() == 0;
	if (no_body || params.size() == 0) {
		if (!no_body)
			res.push_back(body);
		return;
	}

	// {position, param_index}
	vector<pair<size_t, size_t>> found_param_usage;
	found_param_usage.reserve(params.size());

	for (size_t p_i = 0; p_i < params.size(); p_i++) {
		const auto & p = params[p_i];
		size_t start_pos = 0;
		while ((start_pos = body.find(p, start_pos)) != string::npos) {
			if (start_pos > 0) {
				auto c_pre = body[start_pos - 1];
				auto is_part_of_literal = isalnum(c_pre) || '_' == c_pre
						|| '$' == c_pre;
				if (is_part_of_literal) {
					start_pos += 1;
					continue;
				}
				auto s = check_is_in_string(start_pos);
				if (s) {
					assert(start_pos < s->second);
					start_pos = s->second;
					continue;
				}
			}
			auto c_post = body[start_pos + p.length()];

			/*
			 * Test what is next character. If next character is part of [a-zA-Z0-9_$([{] then it is not what we have to replace.
			 * 19.3.1 `define
			 * The text specified for macro text shall not be split across the following lexical tokens:
			 *   Comments
			 *   Numbers
			 *   Strings
			 *   Identifiers
			 *   Keywords
			 *   Operators
			 * */
			auto is_part_of_literal = isalnum(c_post) || '_' == c_post
					|| '$' == c_post || '(' == c_post || '[' == c_post
					|| '{' == c_post;
			//check the find is in the result of a substitution of the same
			// macro_replacement

			if (is_part_of_literal) {
				start_pos += 1;
				continue;
			}
			auto s = check_is_in_string(start_pos);
			if (s) {
				assert(start_pos < s->second);
				start_pos = s->second;
				continue;
			}
			found_param_usage.push_back( { start_pos, p_i });
			start_pos += 1;
		}
	}
	sort(found_param_usage.begin(), found_param_usage.end());
	size_t start = 0;
	for (auto fp : found_param_usage) {
		if (start < fp.first) {
			// add string before param usage
			auto s = body.substr(start, fp.first - start);
			res.push_back(Fragment(s));
		}
		res.push_back(Fragment(fp.second));
		start = fp.first + params[fp.second].length();
	}
	if (start != body.length()) {
		auto s = body.substr(start);
		res.push_back(Fragment(s));
	}
}

// return false to skip this find because it is
// from an already substitution of the same macro replacement
std::pair<size_t, size_t> * MacroDef::check_is_in_string(size_t start) {
	for (auto & p : _string_intervals) {
		if ((p.first <= start) && (start < p.first + p.second)) {
			return &p;
		}
	}
	return nullptr;
}
/*
 * Look for String literal. In order to forbid them to be change by replacement
 */
void MacroDef::collect_string_intervals(const string & tmpl) {
	size_t start_pos = 0;
	auto npos = string::npos;
	size_t pos1 = npos;
	while ((start_pos = tmpl.find('"', start_pos)) != npos) {
		if (pos1 == npos
				&& ((start_pos != 0 && tmpl[start_pos - 1] != '`')
						|| start_pos == 0)) {
			pos1 = start_pos;
		} else if (pos1 != npos && tmpl[start_pos - 1] != '`') {
			size_t length = start_pos - pos1;
			_string_intervals.push_back(make_pair(pos1, length));
			pos1 = npos;
		}
		start_pos += 1;
	}
	if (pos1 != npos)
		throw ParseException(
				"Unfinished string in definition of macro " + name + ".");
}

string MacroDef::replace(vector<string> args, bool args_specified) {
	if (has_params && !args_specified) {
		string msg = "Macro " + name + " requires braces and expects ";
		if (default_args.size()) {
			msg += "(" + std::to_string(params.size() - default_args.size())
					+ " to ";
		} else {
			msg += "(";
		}
		msg += std::to_string(params.size()) + " arguments).";
		throw ParseException(msg);
	}
	if (!has_params && args_specified) {
		string msg = "Macro " + name
				+ " does not expect any argumets or braces.";
		throw ParseException(msg);
	}

	// the number of provided argument is fewer than the number defined by the prototype.
	// So we complete the list with default value.
	if (args.size() <= params.size()) {
		auto orig_args_cnt = args.size();
		for (size_t i = 0; i < params.size(); i++) {
			bool value_required = i >= orig_args_cnt;
			bool use_defult_value = value_required || args[i].length() == 0;
			if (use_defult_value) {
				const auto & p_name = params[i];
				auto def = default_args.find(p_name);
				if (def == default_args.end()) {
					if (!value_required)
						continue;
					string msg = "Macro " + name
							+ " missing value for parameter " + p_name + " ";
					bool is_last = i == params.size() - 1;
					if (!is_last)
						msg += "and for parameters after ";
					if (default_args.size()) {
						msg += "("
								+ std::to_string(
										params.size() - default_args.size())
								+ " to ";
					} else {
						msg += "(";
					}
					msg += std::to_string(params.size())
							+ " arguments expected but "
							+ to_string(orig_args_cnt) + " provided).";
					throw ParseException(msg);
				}
				if (i < orig_args_cnt) {
					args[i] = def->second;
				} else {
					args.push_back(def->second);
				}

			}
		}
	} else if (args.size() > params.size()) {
		string msg = "Macro " + name + " expected ";
		if (default_args.size()) {
			msg += std::to_string(params.size() - default_args.size()) + " to ";
		}
		msg += std::to_string(params.size()) + " arguments but "
				+ to_string(args.size()) + " provided.";
		throw ParseException(msg);
	}

	if (!body.empty()) {
		stringstream res;
		for (const auto & elem : body) {
			if (elem.arg_no < 0) {
				res << elem.str;
			} else {
				res << args.at(elem.arg_no);
			}
		}
		return res.str();
	}
	return "";
}

}
}
