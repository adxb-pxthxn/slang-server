// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "ast/NetTraceTypes.h"
#include "lsp/LspTypes.h"
#include "utils/ServerHarness.h"
#include <algorithm>
#include <string>
#include <vector>

namespace {

std::vector<std::string> hopPaths(const server::TraceSignalResult& result) {
    std::vector<std::string> paths;
    paths.reserve(result.hops.size());
    for (const auto& hop : result.hops)
        paths.push_back(hop.hierPath);
    return paths;
}

bool hasPath(const server::TraceSignalResult& result, std::string_view path) {
    return std::ranges::any_of(result.hops,
                               [&](const server::TraceHop& hop) { return hop.hierPath == path; });
}

const char* kChainSrc = R"(
module grandchild(input logic in_x);
endmodule

module child(input logic port_a);
  grandchild u_gc(.in_x(port_a));
endmodule

module top;
  logic my_signal;
  child u_child(.port_a(my_signal));
endmodule
)";

} // namespace

TEST_CASE("TraceSignal - three-level chain from all cursor sites") {
    ServerHarness server;
    auto doc = server.openFile("chain.sv", kChainSrc);
    server.setTopLevel(std::string{doc.m_uri.getPath()});

    const std::vector<std::string> expected = {"top.my_signal", "top.u_child.port_a",
                                               "top.u_child.u_gc.in_x"};

    auto checkChain = [&](Cursor cursor) {
        auto result = server.traceSignal({.textDocument = {.uri = doc.m_uri},
                                          .position = cursor.getPosition(),
                                          .instancePath = std::nullopt});
        CHECK(result.candidates.empty());
        REQUIRE(result.hops.size() == expected.size());
        CHECK(hasPath(result, "top.my_signal"));
        CHECK(hasPath(result, "top.u_child.port_a"));
        CHECK(hasPath(result, "top.u_child.u_gc.in_x"));
    };

    SECTION("parent net declaration") {
        auto result = server.traceSignal({.textDocument = {.uri = doc.m_uri},
                                          .position = doc.before("my_signal").getPosition(),
                                          .instancePath = std::nullopt});
        REQUIRE_FALSE(result.hops.empty());
        CHECK(result.hops[0].hierPath == "top.my_signal");
        checkChain(doc.before("my_signal"));
    }
    SECTION("named port connection parent net") {
        checkChain(doc.after(".port_a("));
    }
    SECTION("named port connection port name") {
        checkChain(doc.after("u_child(."));
    }
    SECTION("child port declaration") {
        checkChain(doc.after("module child(input logic "));
    }
    SECTION("grandchild port use via connection") {
        checkChain(doc.after(".in_x("));
    }
}

TEST_CASE("TraceSignal - dual instantiation does not cross-talk") {
    ServerHarness server;
    auto doc = server.openFile("dual.sv", R"(
module child(input logic p);
endmodule

module top;
  logic a, b;
  child u1(.p(a));
  child u2(.p(b));
endmodule
)");
    server.setTopLevel(std::string{doc.m_uri.getPath()});

    auto fromA = server.traceSignal({.textDocument = {.uri = doc.m_uri},
                                     .position = doc.after("logic ").getPosition(),
                                     .instancePath = std::nullopt});
    CHECK(fromA.candidates.empty());
    CHECK(hasPath(fromA, "top.a"));
    CHECK(hasPath(fromA, "top.u1.p"));
    CHECK_FALSE(hasPath(fromA, "top.u2.p"));
    CHECK_FALSE(hasPath(fromA, "top.b"));

    auto fromB = server.traceSignal({.textDocument = {.uri = doc.m_uri},
                                     .position = doc.after("logic a, ").getPosition(),
                                     .instancePath = std::nullopt});
    CHECK(hasPath(fromB, "top.b"));
    CHECK(hasPath(fromB, "top.u2.p"));
    CHECK_FALSE(hasPath(fromB, "top.u1.p"));
}

TEST_CASE("TraceSignal - instance array paths are distinct") {
    ServerHarness server;
    auto doc = server.openFile("arr.sv", R"(
module child(input logic p);
endmodule

module top;
  logic a;
  child u[1:0](.p(a));
endmodule
)");
    server.setTopLevel(std::string{doc.m_uri.getPath()});

    auto result = server.traceSignal({.textDocument = {.uri = doc.m_uri},
                                      .position = doc.before("a;").getPosition(),
                                      .instancePath = std::nullopt});
    CHECK(hasPath(result, "top.a"));
    CHECK(hasPath(result, "top.u[0].p"));
    CHECK(hasPath(result, "top.u[1].p"));
}

TEST_CASE("TraceSignal - generate-if instance") {
    ServerHarness server;
    auto doc = server.openFile("gen.sv", R"(
module child(input logic p);
endmodule

module top;
  logic a, b;
  if (1) begin : blk
    child u(.p(a));
  end
  child other(.p(b));
endmodule
)");
    server.setTopLevel(std::string{doc.m_uri.getPath()});

    auto fromA = server.traceSignal({.textDocument = {.uri = doc.m_uri},
                                     .position = doc.after("logic ").getPosition(),
                                     .instancePath = std::nullopt});
    CHECK(hasPath(fromA, "top.a"));
    CHECK(hasPath(fromA, "top.blk.u.p"));
    CHECK_FALSE(hasPath(fromA, "top.other.p"));
    CHECK_FALSE(hasPath(fromA, "top.b"));
}

TEST_CASE("TraceSignal - no compilation returns empty") {
    ServerHarness server;
    auto doc = server.openFile("chain.sv", kChainSrc);

    auto result = server.traceSignal({.textDocument = {.uri = doc.m_uri},
                                      .position = doc.before("my_signal").getPosition(),
                                      .instancePath = std::nullopt});
    CHECK(result.candidates.empty());
    CHECK(result.hops.empty());
}

TEST_CASE("TraceSignal - cursor on comment is empty") {
    ServerHarness server;
    auto doc = server.openFile("chain.sv", kChainSrc);
    server.setTopLevel(std::string{doc.m_uri.getPath()});

    auto result = server.traceSignal({.textDocument = {.uri = doc.m_uri},
                                      .position = doc.begin().getPosition(),
                                      .instancePath = std::nullopt});
    CHECK(result.candidates.empty());
    CHECK(result.hops.empty());
}

TEST_CASE("TraceSignal - wildcard and concat connections are skipped") {
    ServerHarness server;
    auto doc = server.openFile("skip.sv", R"(
module child(input logic p, input logic q);
endmodule

module top;
  logic p, q, a, b;
  child wild(.*);
  child cat(.p({a, b}), .q(q));
endmodule
)");
    server.setTopLevel(std::string{doc.m_uri.getPath()});

    auto fromP = server.traceSignal({.textDocument = {.uri = doc.m_uri},
                                     .position = doc.before("p, q, a, b").getPosition(),
                                     .instancePath = std::nullopt});
    CHECK_FALSE(hasPath(fromP, "top.wild.p"));

    auto fromA = server.traceSignal({.textDocument = {.uri = doc.m_uri},
                                     .position = doc.before("a, b").getPosition(),
                                     .instancePath = std::nullopt});
    CHECK(hasPath(fromA, "top.a"));
    CHECK_FALSE(hasPath(fromA, "top.cat.p"));
}

TEST_CASE("TraceSignal - positional connection") {
    ServerHarness server;
    auto doc = server.openFile("pos.sv", R"(
module child(input logic p);
endmodule

module top;
  logic sig;
  child u(sig);
endmodule
)");
    server.setTopLevel(std::string{doc.m_uri.getPath()});

    auto result = server.traceSignal({.textDocument = {.uri = doc.m_uri},
                                      .position = doc.after("logic ").getPosition(),
                                      .instancePath = std::nullopt});
    CHECK(hasPath(result, "top.sig"));
    CHECK(hasPath(result, "top.u.p"));
}

TEST_CASE("TraceSignal - ambiguous instance returns candidates") {
    ServerHarness server;
    auto doc = server.openFile("dual.sv", R"(
module child(input logic p);
endmodule

module top;
  logic a, b;
  child u1(.p(a));
  child u2(.p(b));
endmodule
)");
    server.setTopLevel(std::string{doc.m_uri.getPath()});

    auto cursor = doc.after("module child(input logic ");
    auto result = server.traceSignal({.textDocument = {.uri = doc.m_uri},
                                      .position = cursor.getPosition(),
                                      .instancePath = std::nullopt});
    REQUIRE(result.candidates.size() == 2);
    CHECK(result.hops.empty());

    auto chosen = server.traceSignal({.textDocument = {.uri = doc.m_uri},
                                      .position = cursor.getPosition(),
                                      .instancePath = std::string{"top.u1.p"}});
    CHECK(chosen.candidates.empty());
    CHECK(hasPath(chosen, "top.u1.p"));
    CHECK(hasPath(chosen, "top.a"));
    CHECK_FALSE(hasPath(chosen, "top.u2.p"));
}

TEST_CASE("TraceSignal - rename of parent net does not rename child port") {
    ServerHarness server;
    auto doc = server.openFile("chain.sv", kChainSrc);
    server.setTopLevel(std::string{doc.m_uri.getPath()});

    auto edit = server.getDocRename(lsp::RenameParams{
        .textDocument = {.uri = doc.m_uri},
        .position = doc.before("my_signal").getPosition(),
        .newName = "renamed",
    });
    REQUIRE(edit.has_value());
    REQUIRE(edit->changes.has_value());

    auto uriStr = doc.m_uri.str();
    REQUIRE(edit->changes->contains(uriStr));

    bool renamedPort = false;
    for (const auto& textEdit : edit->changes->at(uriStr)) {
        CHECK(textEdit.newText == "renamed");
        auto line = doc.getLine(textEdit.range.start.line + 1);
        auto portCol = line.find("port_a");
        if (portCol != std::string_view::npos &&
            textEdit.range.start.character == static_cast<lsp::uint>(portCol)) {
            renamedPort = true;
        }
    }
    CHECK_FALSE(renamedPort);
}
