// Copyright 2011 Google Inc. All Rights Reserved.
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

#ifndef NINJA_BUILD_H_
#define NINJA_BUILD_H_

#include <cstdio>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <string>
#include <vector>

#include "depfile_parser.h"
#include "graph.h"  // XXX needed for DependencyScan; should rearrange.
#include "exit_status.h"
#include "line_printer.h"
#include "metrics.h"
#include "util.h"  // int64_t

struct BuildLog;
struct BuildStatus;
struct Builder;
struct DiskInterface;
struct Edge;
struct Node;
struct State;

/// Plan stores the state of a build plan: what we intend to build,
/// which steps we're ready to execute.
struct Plan {
  Plan(Builder* builder = nullptr);

  /// Add a target to our plan (including all its dependencies).
  /// Returns false if we don't need to build this target; may
  /// fill in |err| with an error message if there's a problem.
  bool AddTarget(Node* node, std::string* err);

  // Pop a ready edge off the queue of edges to build.
  // Returns nullptr if there's no work to do.
  Edge* FindWork();

  /// Returns true if there's more work to be done.
  bool more_to_do() const { return wanted_edges_ > 0 && command_edges_ > 0; }

  /// Dumps the current state of the plan.
  void Dump();

  enum EdgeResult {
    kEdgeFailed,
    kEdgeSucceeded
  };

  /// Mark an edge as done building (whether it succeeded or failed).
  /// If any of the edge's outputs are dyndep bindings of their dependents,
  /// this loads dynamic dependencies from the nodes' paths.
  /// Returns 'false' if loading dyndep info fails and 'true' otherwise.
  bool EdgeFinished(Edge* edge, EdgeResult result, std::string* err);

  /// Clean the given node during the build.
  /// Return false on error.
  bool CleanNode(DependencyScan* scan, Node* node, std::string* err);

  /// Number of edges with commands to run.
  int command_edge_count() const { return command_edges_; }

  /// Reset state.  Clears want and ready sets.
  void Reset();

  /// Update the build plan to account for modifications made to the graph
  /// by information loaded from a dyndep file.
  bool DyndepsLoaded(DependencyScan* scan, Node* node,
                     const DyndepFile& ddf, std::string* err);
private:
  bool RefreshDyndepDependents(DependencyScan* scan, Node* node, std::string* err);
  void UnmarkDependents(Node* node, std::set<Node*>* dependents);
  bool AddSubTarget(Node* node, Node* dependent, std::string* err,
                    std::set<Edge*>* dyndep_walk);

  /// Update plan with knowledge that the given node is up to date.
  /// If the node is a dyndep binding on any of its dependents, this
  /// loads dynamic dependencies from the node's path.
  /// Returns 'false' if loading dyndep info fails and 'true' otherwise.
  bool NodeFinished(Node* node, std::string* err);

  /// Enumerate possible steps we want for an edge.
  enum Want
  {
    /// We do not want to build the edge, but we might want to build one of
    /// its dependents.
    kWantNothing,
    /// We want to build the edge, but have not yet scheduled it.
    kWantToStart,
    /// We want to build the edge, have scheduled it, and are waiting
    /// for it to complete.
    kWantToFinish
  };

  void EdgeWanted(const Edge* edge);
  bool EdgeMaybeReady(std::map<Edge*, Want>::iterator want_e, std::string* err);

  /// Submits a ready edge as a candidate for execution.
  /// The edge may be delayed from running, for example if it's a member of a
  /// currently-full pool.
  void ScheduleWork(std::map<Edge*, Want>::iterator want_e);

  /// Keep track of which edges we want to build in this plan.  If this map does
  /// not contain an entry for an edge, we do not want to build the entry or its
  /// dependents.  If it does contain an entry, the enumeration indicates what
  /// we want for the edge.
  std::map<Edge*, Want> want_;

  std::set<Edge*> ready_;

  Builder* builder_;

  /// Total number of edges that have commands (not phony).
  int command_edges_;

  /// Total remaining number of wanted edges.
  int wanted_edges_;
};

/// CommandRunner is an interface that wraps running the build
/// subcommands.  This allows tests to abstract out running commands.
/// RealCommandRunner is an implementation that actually runs commands.
struct CommandRunner {
  virtual ~CommandRunner() = default;
  virtual bool CanRunMore() const = 0;
  virtual bool StartCommand(Edge* edge) = 0;

  /// The result of waiting for a command.
  struct Result final {
    Result() = default;
    Edge* edge = nullptr;
    ExitStatus status;
    std::string output;
    bool success() const { return status == ExitSuccess; }
  };
  /// Wait for a command to complete, or return false if interrupted.
  virtual bool WaitForCommand(Result* result) = 0;

  virtual std::vector<Edge*> GetActiveEdges() { return std::vector<Edge*>(); }
  virtual void Abort() {}
};

/// Options (e.g. verbosity, parallelism) passed to a build.
struct BuildConfig final {
  BuildConfig() : verbosity(NORMAL), dry_run(false), parallelism(1),
                  failures_allowed(1), max_load_average(-0.0f) {}

  enum Verbosity {
    NORMAL,
    QUIET,  // No output -- used when testing.
    VERBOSE
  };
  Verbosity verbosity;
  bool dry_run;
  int parallelism;
  int failures_allowed;
  /// The maximum load average we must not exceed. A negative value
  /// means that we do not have any limit.
  double max_load_average;
  DepfileParserOptions depfile_parser_options;
};

/// Builder wraps the build process: starting commands, updating status.
struct Builder final {
  Builder(State* state, const BuildConfig& config,
          BuildLog* build_log, DepsLog* deps_log,
          DiskInterface* disk_interface);
  ~Builder();

  /// Clean up after interrupted commands by deleting output files.
  void Cleanup();

  Node* AddTarget(const std::string& name, std::string* err);

  /// Add a target to the build, scanning dependencies.
  /// @return false on error.
  bool AddTarget(Node* target, std::string* err);

  /// Returns true if the build targets are already up to date.
  bool AlreadyUpToDate() const;

  /// Run the build.  Returns false on error.
  /// It is an error to call this function when AlreadyUpToDate() is true.
  bool Build(std::string* err);

  bool StartEdge(Edge* edge, std::string* err);

  /// Update status ninja logs following a command termination.
  /// @return false if the build can not proceed further due to a fatal error.
  bool FinishCommand(CommandRunner::Result* result, std::string* err);

  /// Used for tests.
  void SetBuildLog(BuildLog* log) {
    scan_.set_build_log(log);
  }

  /// Load the dyndep information provided by the given node.
  bool LoadDyndeps(Node* node, std::string* err);

  State* state_;
  const BuildConfig& config_;
  Plan plan_;
  std::unique_ptr<CommandRunner> command_runner_;
  BuildStatus* status_;

 private:
   bool ExtractDeps(CommandRunner::Result* result, const std::string& deps_type,
                    const std::string& deps_prefix, std::vector<Node*>* deps_nodes,
                    std::string* err);

  DiskInterface* disk_interface_;
  DependencyScan scan_;

  // Unimplemented copy ctor and operator= ensure we don't copy the auto_ptr.
  Builder(const Builder &other);        // DO NOT IMPLEMENT
  void operator=(const Builder &other); // DO NOT IMPLEMENT
};

/// Tracks the status of a build: completion fraction, printing updates.
struct BuildStatus final {
  explicit BuildStatus(const BuildConfig& config);
  void PlanHasTotalEdges(int total);
  void BuildEdgeStarted(Edge* edge);
  void BuildEdgeFinished(Edge* edge, bool success, const std::string& output,
                         int* start_time, int* end_time);
  void BuildLoadDyndeps();
  void BuildStarted();
  void BuildFinished();

  enum EdgeStatus {
    kEdgeStarted,
    kEdgeFinished,
  };

  /// Format the progress status string by replacing the placeholders.
  /// See the user manual for more information about the available
  /// placeholders.
  /// @param progress_status_format The format of the progress status.
  /// @param status The status of the edge.
  std::string FormatProgressStatus(const char* progress_status_format,
                              EdgeStatus status) const;

 private:
  void PrintStatus(Edge* edge, EdgeStatus status);

  const BuildConfig& config_;

  /// Time the build started.
  int64_t start_time_millis_;

  int started_edges_ = 0, finished_edges_ = 0, total_edges_ = 0;

  /// Map of running edge to time the edge started running.
  typedef std::map<Edge*, int> RunningEdgeMap;
  RunningEdgeMap running_edges_;

  /// Prints progress output.
  LinePrinter printer_;

  /// The custom progress status format to use.
  const char* progress_status_format_ = nullptr;

  template<size_t S>
  void SnprintfRate(double rate, char(&buf)[S], const char* format) const {
    if (rate == -1)
      snprintf(buf, S, "?");
    else
      snprintf(buf, S, format, rate);
  }

  struct RateInfo final {
    RateInfo() = default;
    void Restart() { stopwatch_.Restart(); }
    double Elapsed() const { return stopwatch_.Elapsed(); }
    double rate() { return rate_; }

    void UpdateRate(int edges) {
      if (edges && stopwatch_.Elapsed())
        rate_ = edges / stopwatch_.Elapsed();
    }

  private:
    double rate_ = -1; // This is awful. Use std::numeric_limits.
    Stopwatch stopwatch_;
  };

  struct SlidingRateInfo final {
    SlidingRateInfo(int n) : N(n) {}

    void Restart() { stopwatch_.Restart(); }
    double rate() { return rate_; }

    void UpdateRate(int update_hint) {
      if (update_hint == last_update_)
        return;
      last_update_ = update_hint;

      if (times_.size() == N)
        times_.pop();
      times_.push(stopwatch_.Elapsed());
      if (times_.back() != times_.front())
        rate_ = times_.size() / (times_.back() - times_.front());
    }

  private:
    double rate_ = -1; // TODO: Use std::numeric_limts
    Stopwatch stopwatch_;
    const size_t N;
    std::queue<double> times_;
    int last_update_ = -1; // TODO: Use std::numeric_limits
  };

  mutable RateInfo overall_rate_;
  mutable SlidingRateInfo current_rate_;
};

#endif  // NINJA_BUILD_H_
