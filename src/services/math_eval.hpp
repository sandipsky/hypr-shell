#pragma once

#include <optional>
#include <string>

namespace hyprshell::math_eval {

// Port of Noctalia's AdvancedMath.js + the CalculatorProvider expression
// gate, for the launcher's inline calculator. Pure functions, no I/O.

// CalculatorProvider.isMathExpression: cheap gate so app names ("firefox")
// never reach the evaluator.
bool is_math_expression(const std::string& expr);

// AdvancedMath.evaluate: +-*/%^, parentheses, the whitelisted functions
// (sin/cos/tan[d], asin…, sinh…, log/ln/exp/pow, sqrt/cbrt, abs/floor/ceil/
// round/trunc, min/max, atan2, random) and the constants pi/e.
// nullopt on any parse error or non-finite result.
std::optional<double> evaluate(const std::string& expr);

// AdvancedMath.formatResult: integers plain, very large/small numbers in
// exponential form, otherwise up to 10 decimals with trailing zeros dropped.
std::string format_result(double value);

} // namespace hyprshell::math_eval
