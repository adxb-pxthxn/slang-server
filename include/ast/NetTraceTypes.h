//------------------------------------------------------------------------------
// NetTraceTypes.h
// LSP types for slang.traceSignal (cross-module net name tracing)
//
// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include "lsp/LspTypes.h"
#include <optional>
#include <string>
#include <vector>

namespace server {

struct TraceSignalParams {
    lsp::TextDocumentIdentifier textDocument;
    lsp::Position position;
    std::optional<std::string> instancePath;
};

struct TraceHop {
    std::string hierPath;
    std::string name;
    lsp::Location location;
};

struct TraceSignalResult {
    std::vector<std::string> candidates;
    std::vector<TraceHop> hops;
};

} // namespace server
