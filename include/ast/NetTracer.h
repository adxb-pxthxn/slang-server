//------------------------------------------------------------------------------
// NetTracer.h
// Builds a path-keyed alias graph from simple instance port connections
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "ast/NetTraceTypes.h"
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/text/SourceManager.h"

namespace server {

/// Undirected alias graph of nets connected through simple port connections.
/// Nodes are hierarchical RTL paths (not ValueSymbol*), because slang shares
/// instance bodies across instantiations.
class NetTracer : public slang::ast::ASTVisitor<NetTracer, slang::ast::VisitFlags::Symbols> {
public:
    static constexpr size_t maxDepth = 64;

    explicit NetTracer(slang::ast::Compilation& compilation,
                       const slang::SourceManager& sourceManager);

    std::vector<TraceHop> trace(std::string_view startPath) const;

    void handle(const slang::ast::InstanceSymbol& inst);

private:
    struct Node {
        std::string hierPath;
        std::string name;
        lsp::Location location;
    };

    void recordConnections(const slang::ast::InstanceSymbol& inst);
    void addEdge(const slang::ast::Symbol& parentSym, const slang::ast::Symbol& childSym);
    Node makeNode(const slang::ast::Symbol& symbol) const;
    TraceHop hopFromSymbol(const slang::ast::Symbol& symbol) const;

    slang::ast::Compilation& m_compilation;
    const slang::SourceManager& m_sourceManager;
    std::unordered_map<std::string, Node> m_nodes;
    std::unordered_map<std::string, std::vector<std::string>> m_adj;
};

} // namespace server
