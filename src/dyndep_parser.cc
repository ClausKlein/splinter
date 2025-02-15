// Copyright 2015 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "dyndep_parser.h"

#include <vector>

#include "dyndep.h"
#include "graph.h"
#include "state.h"
#include "util.h"
#include "version.h"
#include "string_concat.h"

DyndepParser::DyndepParser(State* state, FileReader* file_reader,
                           DyndepFile* dyndep_file)
    : Parser(state, file_reader)
    , dyndep_file_(dyndep_file) {
}

bool DyndepParser::Parse(std::filesystem::path const& filename, const std::string& input,
                         std::string* err) {
  lexer_.Start(filename, input);

  // Require a supported ninja_dyndep_version value immediately so
  // we can exit before encountering any syntactic surprises.
  bool haveDyndepVersion = false;

  for (;;) {
    Lexer::Token token = lexer_.ReadToken();
    switch (token) {
    case Lexer::BUILD: {
      if (!haveDyndepVersion)
        return lexer_.Error("expected 'ninja_dyndep_version = ...'", err);
      if (!ParseEdge(err))
        return false;
      break;
    }
    case Lexer::IDENT: {
      lexer_.UnreadToken();
      if (haveDyndepVersion)
        return lexer_.Error(std::string("unexpected ") + Lexer::TokenName(token),
                            err);
      if (!ParseDyndepVersion(err))
        return false;
      haveDyndepVersion = true;
      break;
    }
    case Lexer::ERROR:
      return lexer_.Error(lexer_.DescribeLastError(), err);
    case Lexer::TEOF:
      if (!haveDyndepVersion)
        return lexer_.Error("expected 'ninja_dyndep_version = ...'", err);
      return true;
    case Lexer::NEWLINE:
      break;
    default:
      return lexer_.Error(std::string("unexpected ") + Lexer::TokenName(token),
                          err);
    }
  }
  return false;  // not reached
}

bool DyndepParser::ParseDyndepVersion(std::string* err) {
  std::string name;
  EvalString let_value;
  if (!ParseLet(&name, &let_value, err))
    return false;
  if (name != "ninja_dyndep_version") {
    return lexer_.Error("expected 'ninja_dyndep_version = ...'", err);
  }
  std::string version = let_value.Evaluate(&env_);
  auto const& [major, minor] = ParseVersion(version);
  if (major != 1 || minor != 0) {
    return lexer_.Error(
      std::string("unsupported 'ninja_dyndep_version = ") + version + "'", err);
    return false;
  }
  return true;
}

bool DyndepParser::ParseLet(std::string* key, EvalString* value, std::string* err) {
  if (!lexer_.ReadIdent(key))
    return lexer_.Error("expected variable name", err);
  if (!ExpectToken(Lexer::EQUALS, err))
    return false;
  if (!lexer_.ReadVarValue(value, err))
    return false;
  return true;
}

bool DyndepParser::ParseEdge(std::string* err) {
  // Parse one explicit output.  We expect it to already have an edge.
  // We will record its dynamically-discovered dependency information.
  Dyndeps* dyndeps = nullptr;
  {
    EvalString out0;
    if (!lexer_.ReadPath(&out0, err))
      return false;
    if (out0.empty())
      return lexer_.Error("expected path", err);

    std::string path = out0.Evaluate(&env_);
    Node* node = state_->LookupNode(path);
    if (!node || !node->in_edge())
      return lexer_.Error(string_concat("no build statement exists for '", path, "'"), err);
    Edge* edge = node->in_edge();
    auto const& [it, success] = dyndep_file_->emplace(edge, Dyndeps());
    if ( ! success)
    {
      return lexer_.Error(string_concat("multiple statements for '", path, "'"), err);
    }
    dyndeps = &(it->second);
  }

  // Disallow explicit outputs.
  {
    EvalString out;
    if (!lexer_.ReadPath(&out, err))
      return false;
    if (!out.empty())
      return lexer_.Error("explicit outputs not supported", err);
  }

  // Parse implicit outputs, if any.
  std::vector<EvalString> outs;
  if (lexer_.PeekToken(Lexer::PIPE)) {
    for (;;) {
      EvalString out;
      if (!lexer_.ReadPath(&out, err))
        return err;
      if (out.empty())
        break;
      outs.push_back(out);
    }
  }

  if (!ExpectToken(Lexer::COLON, err))
    return false;

  std::string rule_name;
  if (!lexer_.ReadIdent(&rule_name) || rule_name != "dyndep")
    return lexer_.Error("expected build command name 'dyndep'", err);

  // Disallow explicit inputs.
  {
    EvalString in;
    if (!lexer_.ReadPath(&in, err))
      return false;
    if (!in.empty())
      return lexer_.Error("explicit inputs not supported", err);
  }

  // Parse implicit inputs, if any.
  std::vector<EvalString> ins;
  if (lexer_.PeekToken(Lexer::PIPE)) {
    for (;;) {
      EvalString in;
      if (!lexer_.ReadPath(&in, err))
        return err;
      if (in.empty())
        break;
      ins.push_back(in);
    }
  }

  // Disallow order-only inputs.
  if (lexer_.PeekToken(Lexer::PIPE2))
    return lexer_.Error("order-only inputs not supported", err);

  if (!ExpectToken(Lexer::NEWLINE, err))
    return false;

  if (lexer_.PeekToken(Lexer::INDENT)) {
    std::string key;
    EvalString val;
    if (!ParseLet(&key, &val, err))
      return false;
    if (key != "restat")
      return lexer_.Error("binding is not 'restat'", err);
    std::string value = val.Evaluate(&env_);
    dyndeps->restat_ = !value.empty();
  }

  dyndeps->implicit_inputs_.reserve(ins.size());
  for (auto const& item : ins) {
    std::string path = item.Evaluate(&env_);
    Node* n = state_->GetNode(path);
    dyndeps->implicit_inputs_.push_back(n);
  }

  for (auto const& item : outs) {
    std::string path = item.Evaluate(&env_);\
    Node* n = state_->GetNode(path);
    dyndeps->implicit_outputs_.push_back(n);
  }

  return true;
}
