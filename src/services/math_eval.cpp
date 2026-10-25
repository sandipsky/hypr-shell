#include "services/math_eval.hpp"

#include <glib.h>

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace hyprshell::math_eval {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kE = 2.71828182845904523536;
constexpr double kDegToRad = kPi / 180.0;

// Recursive-descent parser over a lowercased, whitespace-free expression.
// Grammar (matching what Noctalia's JS eval accepted):
//   expr    := term (('+'|'-') term)*
//   term    := power (('*'|'/'|'%') power)*
//   power   := unary ('^' power)?          // right-associative
//   unary   := ('+'|'-')* primary
//   primary := number | name '(' args ')' | 'pi' | 'e' | '(' expr ')'
struct Parser {
    const std::string& s;
    std::size_t pos = 0;
    bool ok = true;

    explicit Parser(const std::string& text) : s(text) {}

    bool eof() const { return pos >= s.size(); }
    char peek() const { return eof() ? '\0' : s[pos]; }
    bool consume(char c) {
        if (peek() != c)
            return false;
        ++pos;
        return true;
    }
    double fail() {
        ok = false;
        return 0.0;
    }

    double parse_expr() {
        double value = parse_term();
        while (ok) {
            if (consume('+'))
                value += parse_term();
            else if (consume('-'))
                value -= parse_term();
            else
                break;
        }
        return value;
    }

    double parse_term() {
        double value = parse_power();
        while (ok) {
            if (consume('*'))
                value *= parse_power();
            else if (consume('/'))
                value /= parse_power();
            else if (consume('%'))
                value = std::fmod(value, parse_power());
            else
                break;
        }
        return value;
    }

    double parse_power() {
        double base = parse_unary();
        if (ok && consume('^'))
            return std::pow(base, parse_power());
        return base;
    }

    double parse_unary() {
        bool negative = false;
        while (peek() == '+' || peek() == '-') {
            if (s[pos] == '-')
                negative = !negative;
            ++pos;
        }
        const double value = parse_primary();
        return negative ? -value : value;
    }

    double parse_primary() {
        if (consume('(')) {
            const double value = parse_expr();
            if (!consume(')'))
                return fail();
            return value;
        }
        if (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '.')
            return parse_number();
        if (std::isalpha(static_cast<unsigned char>(peek())))
            return parse_name();
        return fail();
    }

    double parse_number() {
        const char* begin = s.data() + pos;
        const char* end = s.data() + s.size();
        double value = 0.0;
        // from_chars is locale-independent (the shell runs under the user's
        // locale, where "%f" parsing could expect a decimal comma)
        auto [next, ec] = std::from_chars(begin, end, value);
        if (ec != std::errc() || next == begin)
            return fail();
        pos += static_cast<std::size_t>(next - begin);
        return value;
    }

    double parse_name() {
        const std::size_t start = pos;
        while (std::isalnum(static_cast<unsigned char>(peek())))
            ++pos;
        const std::string name = s.substr(start, pos - start);

        if (name == "pi")
            return kPi;
        if (name == "e")
            return kE;

        if (!consume('('))
            return fail();
        std::vector<double> args;
        if (!consume(')')) {
            do {
                args.push_back(parse_expr());
            } while (ok && consume(','));
            if (!ok || !consume(')'))
                return fail();
        }
        return apply(name, args);
    }

    double apply(const std::string& name, const std::vector<double>& args) {
        auto one = [&](double (*fn)(double)) {
            return args.size() == 1 ? fn(args[0]) : fail();
        };
        if (name == "sin") return one(std::sin);
        if (name == "cos") return one(std::cos);
        if (name == "tan") return one(std::tan);
        if (name == "asin") return one(std::asin);
        if (name == "acos") return one(std::acos);
        if (name == "atan") return one(std::atan);
        if (name == "sinh") return one(std::sinh);
        if (name == "cosh") return one(std::cosh);
        if (name == "tanh") return one(std::tanh);
        if (name == "asinh") return one(std::asinh);
        if (name == "acosh") return one(std::acosh);
        if (name == "atanh") return one(std::atanh);
        if (name == "log") return one(std::log10);
        if (name == "ln") return one(std::log);
        if (name == "exp") return one(std::exp);
        if (name == "sqrt") return one(std::sqrt);
        if (name == "cbrt") return one(std::cbrt);
        if (name == "abs") return one(std::fabs);
        if (name == "floor") return one(std::floor);
        if (name == "ceil") return one(std::ceil);
        if (name == "round") return one(std::round);
        if (name == "trunc") return one(std::trunc);
        if (name == "sind")
            return args.size() == 1 ? std::sin(args[0] * kDegToRad) : fail();
        if (name == "cosd")
            return args.size() == 1 ? std::cos(args[0] * kDegToRad) : fail();
        if (name == "tand")
            return args.size() == 1 ? std::tan(args[0] * kDegToRad) : fail();
        if (name == "atan2")
            return args.size() == 2 ? std::atan2(args[0], args[1]) : fail();
        if (name == "pow")
            return args.size() == 2 ? std::pow(args[0], args[1]) : fail();
        if (name == "min" || name == "max") {
            if (args.empty())
                return fail();
            double value = args[0];
            for (double a : args)
                value = name == "min" ? std::min(value, a) : std::max(value, a);
            return value;
        }
        if (name == "random") {
            if (!args.empty())
                return fail();
            static std::mt19937 rng{std::random_device{}()};
            return std::uniform_real_distribution<double>(0.0, 1.0)(rng);
        }
        return fail();
    }
};

bool is_operator(char c) {
    return c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '^';
}

} // namespace

bool is_math_expression(const std::string& expr) {
    if (expr.empty())
        return false;

    // Allow: digits, operators, parentheses, decimal points, whitespace,
    // letters (functions), commas — CalculatorProvider.isMathExpression.
    bool has_operator = false;
    bool has_call = false; // letter followed by '('
    bool only_letters = true;
    char last_meaningful = '\0';
    char prev = '\0';
    for (char c : expr) {
        const bool letter = std::isalpha(static_cast<unsigned char>(c));
        const bool allowed = std::isdigit(static_cast<unsigned char>(c)) || letter ||
                             std::isspace(static_cast<unsigned char>(c)) ||
                             is_operator(c) || c == '(' || c == ')' || c == '.' ||
                             c == ',';
        if (!allowed)
            return false;
        if (is_operator(c))
            has_operator = true;
        if (c == '(' && std::isalpha(static_cast<unsigned char>(prev)))
            has_call = true;
        if (!letter && !std::isspace(static_cast<unsigned char>(c)))
            only_letters = false;
        if (!std::isspace(static_cast<unsigned char>(c)))
            last_meaningful = c;
        if (!std::isspace(static_cast<unsigned char>(c)))
            prev = c;
    }
    // Must contain an operator or a function call; reject incomplete
    // expressions and plain words (those are app searches).
    if (!has_operator && !has_call)
        return false;
    if (is_operator(last_meaningful))
        return false;
    if (only_letters)
        return false;
    return true;
}

std::optional<double> evaluate(const std::string& expr) {
    std::string clean;
    clean.reserve(expr.size());
    for (char c : expr)
        if (!std::isspace(static_cast<unsigned char>(c)))
            clean.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (clean.empty())
        return std::nullopt;

    Parser parser(clean);
    const double value = parser.parse_expr();
    if (!parser.ok || !parser.eof() || !std::isfinite(value))
        return std::nullopt;
    return value;
}

std::string format_result(double value) {
    // g_ascii_* keeps the decimal point locale-independent.
    if (value == std::floor(value) && std::fabs(value) < 1e15) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.0f", value);
        return buf;
    }
    char buf[G_ASCII_DTOSTR_BUF_SIZE];
    if (std::fabs(value) >= 1e15 || (std::fabs(value) < 1e-6 && value != 0.0))
        return g_ascii_formatd(buf, sizeof buf, "%.6e", value);
    g_ascii_formatd(buf, sizeof buf, "%.10f", value);
    std::string text = buf;
    while (!text.empty() && text.back() == '0')
        text.pop_back();
    if (!text.empty() && text.back() == '.')
        text.pop_back();
    return text;
}

} // namespace hyprshell::math_eval
