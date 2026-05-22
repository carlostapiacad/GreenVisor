#include "features/pit_optimization/engine/EconomicFormula.h"

#include <QRegularExpression>
#include <QSet>

#include <cmath>
#include <functional>

namespace
{
class EconomicFormulaParser
{
public:
    EconomicFormulaParser(const QString &expression, const std::function<bool(const QString &, double &)> &resolver)
        : m_expression(expression), m_resolver(resolver)
    {
    }

    bool parse(double *value)
    {
        m_pos = 0;
        const bool ok = parseExpression(value);
        skipSpaces();
        return ok && m_pos >= m_expression.size();
    }

private:
    bool parseExpression(double *value)
    {
        if (!parseTerm(value)) {
            return false;
        }
        while (true) {
            skipSpaces();
            if (match('+')) {
                double rhs = 0.0;
                if (!parseTerm(&rhs)) {
                    return false;
                }
                *value += rhs;
            } else if (match('-')) {
                double rhs = 0.0;
                if (!parseTerm(&rhs)) {
                    return false;
                }
                *value -= rhs;
            } else {
                return true;
            }
        }
    }

    bool parseTerm(double *value)
    {
        if (!parseFactor(value)) {
            return false;
        }
        while (true) {
            skipSpaces();
            if (match('*')) {
                double rhs = 0.0;
                if (!parseFactor(&rhs)) {
                    return false;
                }
                *value *= rhs;
            } else if (match('/')) {
                double rhs = 0.0;
                if (!parseFactor(&rhs) || std::abs(rhs) < 1e-12) {
                    return false;
                }
                *value /= rhs;
            } else {
                return true;
            }
        }
    }

    bool parseFactor(double *value)
    {
        skipSpaces();
        if (match('+')) {
            return parseFactor(value);
        }
        if (match('-')) {
            if (!parseFactor(value)) {
                return false;
            }
            *value = -*value;
            return true;
        }
        if (match('(')) {
            if (!parseExpression(value)) {
                return false;
            }
            skipSpaces();
            return match(')');
        }
        if (peek() == '[') {
            return parseBracketReference(value);
        }
        if (peek().isLetter() || peek() == '_') {
            return parseIdentifier(value);
        }
        return parseNumber(value);
    }

    bool parseNumber(double *value)
    {
        skipSpaces();
        const int start = m_pos;
        bool hasDigit = false;
        while (m_pos < m_expression.size()) {
            const QChar ch = m_expression[m_pos];
            if (ch.isDigit()) {
                hasDigit = true;
                ++m_pos;
            } else if (ch == '.') {
                ++m_pos;
            } else {
                break;
            }
        }
        if (m_pos < m_expression.size() && (m_expression[m_pos] == 'e' || m_expression[m_pos] == 'E')) {
            ++m_pos;
            if (m_pos < m_expression.size() && (m_expression[m_pos] == '+' || m_expression[m_pos] == '-')) {
                ++m_pos;
            }
            while (m_pos < m_expression.size() && m_expression[m_pos].isDigit()) {
                hasDigit = true;
                ++m_pos;
            }
        }
        if (!hasDigit) {
            return false;
        }
        bool ok = false;
        *value = m_expression.mid(start, m_pos - start).toDouble(&ok);
        return ok;
    }

    bool parseIdentifier(double *value)
    {
        const int start = m_pos;
        while (m_pos < m_expression.size()) {
            const QChar ch = m_expression[m_pos];
            if (ch.isLetterOrNumber() || ch == '_' || ch == '.') {
                ++m_pos;
            } else {
                break;
            }
        }
        const QString name = m_expression.mid(start, m_pos - start).trimmed();
        skipSpaces();
        if (match('(')) {
            if (name.compare("IF", Qt::CaseInsensitive) == 0) {
                return parseIf(value);
            }
            return false;
        }
        return m_resolver(name, *value);
    }

    bool parseIf(double *value)
    {
        double condition = 0.0;
        if (!parseExpression(&condition)) {
            return false;
        }
        skipSpaces();
        if (!match(',')) {
            return false;
        }
        double trueValue = 0.0;
        if (!parseExpression(&trueValue)) {
            return false;
        }
        skipSpaces();
        if (!match(',')) {
            return false;
        }
        double falseValue = 0.0;
        if (!parseExpression(&falseValue)) {
            return false;
        }
        skipSpaces();
        if (!match(')')) {
            return false;
        }
        *value = std::abs(condition) > 1e-12 ? trueValue : falseValue;
        return true;
    }

    bool parseBracketReference(double *value)
    {
        if (!match('[')) {
            return false;
        }
        const int start = m_pos;
        while (m_pos < m_expression.size() && m_expression[m_pos] != ']') {
            ++m_pos;
        }
        if (m_pos >= m_expression.size()) {
            return false;
        }
        const QString name = m_expression.mid(start, m_pos - start).trimmed();
        ++m_pos;
        return m_resolver(name, *value);
    }

    void skipSpaces()
    {
        while (m_pos < m_expression.size() && m_expression[m_pos].isSpace()) {
            ++m_pos;
        }
    }

    QChar peek() const
    {
        return m_pos < m_expression.size() ? m_expression[m_pos] : QChar();
    }

    bool match(QChar expected)
    {
        skipSpaces();
        if (m_pos < m_expression.size() && m_expression[m_pos] == expected) {
            ++m_pos;
            return true;
        }
        return false;
    }

    QString m_expression;
    std::function<bool(const QString &, double &)> m_resolver;
    int m_pos = 0;
};

double EvaluateEconomicFormulaValueInternal(
    const QString &text,
    const QHash<QString, double> &context,
    const QList<EconomicModelDefinition::Variable> &variables,
    bool *ok,
    QSet<QString> stack)
{
    QString expression = text.trimmed();
    if (expression.isEmpty()) {
        if (ok) {
            *ok = false;
        }
        return 0.0;
    }
    if (expression.startsWith('=')) {
        expression.remove(0, 1);
    }

    auto resolveByName = [&context, &variables, &stack](const QString &name, double &value) mutable -> bool {
        const QString key = name.trimmed();
        for (auto it = context.constBegin(); it != context.constEnd(); ++it) {
            if (it.key().compare(key, Qt::CaseInsensitive) == 0) {
                value = it.value();
                return true;
            }
        }
        for (const auto &variable : variables) {
            if (variable.name.compare(key, Qt::CaseInsensitive) == 0) {
                if (stack.contains(variable.name)) {
                    return false;
                }
                stack.insert(variable.name);
                bool nestedOk = false;
                value = EvaluateEconomicFormulaValueInternal(variable.formula, context, variables, &nestedOk, stack);
                stack.remove(variable.name);
                return nestedOk;
            }
        }
        return false;
    };

    double value = 0.0;
    EconomicFormulaParser parser(expression, resolveByName);
    const bool parsed = parser.parse(&value);
    if (ok) {
        *ok = parsed;
    }
    return parsed ? value : 0.0;
}
}

double EvaluateEconomicFormulaValue(
    const QString &text,
    const QHash<QString, double> &context,
    const QList<EconomicModelDefinition::Variable> &variables,
    bool *ok)
{
    return EvaluateEconomicFormulaValueInternal(text, context, variables, ok, {});
}

bool EvaluateEconomicDestinationValue(
    const QString &text,
    const QHash<QString, double> &context,
    const QList<EconomicModelDefinition::Variable> &variables,
    double *value)
{
    bool numberOk = false;
    const double number = text.trimmed().toDouble(&numberOk);
    if (numberOk) {
        if (value) {
            *value = number;
        }
        return true;
    }
    static const QRegularExpression variableNamePattern(R"(^[A-Za-z_][A-Za-z0-9_]*$)");
    if (!variableNamePattern.match(text.trimmed()).hasMatch()) {
        return false;
    }
    bool ok = false;
    const double parsed = EvaluateEconomicFormulaValue(text, context, variables, &ok);
    if (ok && value) {
        *value = parsed;
    }
    return ok;
}
