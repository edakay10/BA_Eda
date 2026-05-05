#ifndef SEARCH_ALGORITHMS_ENFORCED_HILL_CLIMBING_SEARCH_H
#define SEARCH_ALGORITHMS_ENFORCED_HILL_CLIMBING_SEARCH_H

#include "../evaluation_context.h"
#include "../open_list.h"
#include "../search_algorithm.h"

#include <map>
#include <memory>
#include <utility>
#include <set>
#include <vector>

namespace plugins {
class Options;
}

namespace enforced_hill_climbing_search {
enum class PreferredUsage {
    PRUNE_BY_PREFERRED,
    RANK_PREFERRED_FIRST
};

/*
  Enforced hill-climbing with deferred evaluation.

  TODO: We should test if this lazy implementation really has any benefits over
  an eager one. We hypothesize that both versions need to evaluate and store
  the same states anyways.
*/
class EnforcedHillClimbingSearch : public SearchAlgorithm {
    std::unique_ptr<EdgeOpenList> open_list; // state or edge open list for lazy and non lazy
    
    // Adding on/off toggle for the two main differences between FF and FD
    std::unordered_set<StateID> local_closed_list; // Local closed list (cleared each phase)
    std::unordered_set<StateID> dead_end_list;     // Dead-end states

    bool lazy_evaluation;
    bool global_closed_list;
    bool dead_end_pruning;
  
    std::shared_ptr<Evaluator> evaluator;
    std::vector<std::shared_ptr<Evaluator>> preferred_operator_evaluators;
    std::set<Evaluator *> path_dependent_evaluators;
    bool use_preferred;
    PreferredUsage preferred_usage;

    EvaluationContext current_eval_context;
    int current_phase_start_g;

    // Statistics
    std::map<int, std::pair<int, int>> d_counts;
    int num_ehc_phases;
    int last_num_expanded;

    void insert_successor_into_open_list(
        const EvaluationContext &eval_context, int parent_g, OperatorID op_id,
        bool preferred);
    void expand(EvaluationContext &eval_context);
    void reach_state(const State &parent, OperatorID op_id, const State &state);
    SearchStatus ehc();

protected:
    virtual void initialize() override;
    virtual SearchStatus step() override;

public:
    EnforcedHillClimbingSearch(
        const std::shared_ptr<Evaluator> &h, PreferredUsage preferred_usage,
        const std::vector<std::shared_ptr<Evaluator>> &preferred,
        bool lazy, bool global_closed, bool dead_end, // Added for the on/off toggle
        OperatorCost cost_type, int bound, double max_time,
        const std::string &description, utils::Verbosity verbosity); 

    virtual void print_statistics() const override;
};
}

#endif
