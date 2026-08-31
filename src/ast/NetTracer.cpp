//------------------------------------------------------------------------------
// NetTracer.cpp
// Path-keyed alias graph from simple instance port connections
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------

#include "ast/NetTracer.h"

#include "util/Converters.h"
#include <queue>
#include <unordered_set>
#include <utility>

#include "slang/ast/ASTContext.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/Lookup.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/ConversionExpression.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/ValueSymbol.h"
#include "slang/syntax/AllSyntax.h"

namespace server {
using namespace slang;
using namespace slang::ast;
using namespace slang::syntax;

namespace {

bool isWildcardConnection(const Expression& expr) {
    const SyntaxNode* node = expr.syntax;
    while (node) {
        if (node->kind == SyntaxKind::WildcardPortConnection)
            return true;
        node = node->parent;
    }
    return false;
}

void collectSimpleValues(const Expression& expr, std::vector<const ValueSymbol*>& out) {
    if (auto* conv = expr.as_if<ConversionExpression>()) {
        collectSimpleValues(conv->operand(), out);
        return;
    }
    if (auto* assign = expr.as_if<AssignmentExpression>()) {
        collectSimpleValues(assign->left(), out);
        collectSimpleValues(assign->right(), out);
        return;
    }
    if (auto* named = expr.as_if<NamedValueExpression>())
        out.push_back(&named->symbol);
}

const ValueSymbol* parentSideValue(const Expression& expr, const Symbol* portInternal) {
    std::vector<const ValueSymbol*> values;
    collectSimpleValues(expr, values);

    const ValueSymbol* found = nullptr;
    for (auto* value : values) {
        if (value == portInternal)
            continue;
        if (found)
            return nullptr;
        found = value;
    }
    return found;
}

} // namespace

NetTracer::NetTracer(Compilation& compilation, const SourceManager& sourceManager) :
    m_compilation(compilation), m_sourceManager(sourceManager) {
    compilation.getRoot().visit(*this);
}

void NetTracer::handle(const InstanceSymbol& inst) {
    if (inst.body.flags.has(InstanceFlags::Uninstantiated))
        return;

    auto& body = const_cast<InstanceBodySymbol&>(inst.body);
    const auto* prevParent = std::exchange(body.parentInstance, &inst);

    recordConnections(inst);
    visitDefault(inst);

    body.parentInstance = prevParent;
}

void NetTracer::recordConnections(const InstanceSymbol& inst) {
    for (auto* connection : inst.getPortConnections()) {
        const auto* port = connection->port.as_if<PortSymbol>();
        if (!port || !port->internalSymbol || port->isNullPort)
            continue;

        const Expression* expr = connection->getExpression();
        if (!expr || isWildcardConnection(*expr))
            continue;

        const ValueSymbol* parentVal = parentSideValue(*expr, port->internalSymbol);
        if (!parentVal)
            continue;

        addEdge(*parentVal, *port->internalSymbol);
    }
}

void NetTracer::addEdge(const Symbol& parentSym, const Symbol& childSym) {
    Node parent = makeNode(parentSym);
    Node child = makeNode(childSym);
    if (parent.hierPath.empty() || child.hierPath.empty() || parent.hierPath == child.hierPath)
        return;

    m_nodes.insert({parent.hierPath, parent});
    m_nodes.insert({child.hierPath, child});
    m_adj[parent.hierPath].push_back(child.hierPath);
    m_adj[child.hierPath].push_back(parent.hierPath);
}

NetTracer::Node NetTracer::makeNode(const Symbol& symbol) const {
    SourceLocation start = symbol.location;
    SourceLocation end(start.buffer(), start.offset() + symbol.name.length());
    return Node{
        .hierPath = symbol.getHierarchicalPath(),
        .name = std::string(symbol.name),
        .location = toLocation(SourceRange(start, end), m_sourceManager),
    };
}

TraceHop NetTracer::hopFromSymbol(const Symbol& symbol) const {
    auto node = makeNode(symbol);
    return TraceHop{
        .hierPath = std::move(node.hierPath),
        .name = std::move(node.name),
        .location = node.location,
    };
}

std::vector<TraceHop> NetTracer::trace(std::string_view startPath) const {
    std::string start{startPath};
    if (!m_nodes.contains(start)) {
        ast::LookupResult result;
        ast::ASTContext context(m_compilation.getRoot(), ast::LookupLocation::max);
        ast::Lookup::name(m_compilation.parseName(start), context, ast::LookupFlags::None, result);
        if (!result.found)
            return {};
        return {hopFromSymbol(*result.found)};
    }

    std::vector<TraceHop> hops;
    std::unordered_set<std::string> seen;
    std::queue<std::pair<std::string, size_t>> q;
    q.emplace(start, 0);
    seen.insert(start);

    while (!q.empty()) {
        auto [path, depth] = q.front();
        q.pop();
        if (depth > maxDepth)
            continue;

        auto it = m_nodes.find(path);
        hops.push_back(TraceHop{
            .hierPath = it->second.hierPath,
            .name = it->second.name,
            .location = it->second.location,
        });

        auto adj = m_adj.find(path);
        if (adj == m_adj.end())
            continue;
        for (const auto& next : adj->second) {
            if (seen.insert(next).second)
                q.emplace(next, depth + 1);
        }
    }
    return hops;
}

} // namespace server
