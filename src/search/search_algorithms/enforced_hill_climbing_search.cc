#include "enforced_hill_climbing_search.h"

#include "../algorithms/ordered_set.h"
#include "../evaluators/g_evaluator.h"
#include "../evaluators/pref_evaluator.h"
#include "../open_lists/best_first_open_list.h"
#include "../open_lists/tiebreaking_open_list.h"
#include "../plugins/plugin.h"
#include "../task_utils/successor_generator.h"
#include "../utils/logging.h"
#include "../utils/system.h"

using namespace std;
using utils::ExitCode;

namespace enforced_hill_climbing_search {
using GEval = g_evaluator::GEvaluator;
using PrefEval = pref_evaluator::PrefEvaluator;

static shared_ptr<OpenListFactory> create_ehc_open_list_factory(
    utils::Verbosity verbosity, bool use_preferred,
    PreferredUsage preferred_usage) {
    /*
      TODO: this g-evaluator should probably be set up to always
      ignore costs since EHC is supposed to implement a breadth-first
      search, not a uniform-cost search. So this seems to be a bug.
    */
    shared_ptr<Evaluator> g_evaluator =
        make_shared<GEval>("ehc.g_eval", verbosity);

    if (!use_preferred ||
        preferred_usage == PreferredUsage::PRUNE_BY_PREFERRED) {
        return make_shared<standard_scalar_open_list::BestFirstOpenListFactory>(
            g_evaluator, false);
    } else {
          /*
            TODO: Reduce code duplication with search_common.cc,
            function create_astar_open_list_factory_and_f_eval.

            It would probably make sense to add a factory function or
            constructor that encapsulates this work to the tie-breaking
            open list code.
          */
        vector<shared_ptr<Evaluator>> evals = {
            g_evaluator, make_shared<PrefEval>("ehc.pref_eval", verbosity)};
        return make_shared<tiebreaking_open_list::TieBreakingOpenListFactory>(
            evals, false, true);
    }
}

EnforcedHillClimbingSearch::EnforcedHillClimbingSearch(
    const shared_ptr<Evaluator> &h, PreferredUsage preferred_usage,
    const vector<shared_ptr<Evaluator>> &preferred,
    bool lazy, bool global_closed, bool dead_end,
    OperatorCost cost_type, int bound, double max_time,
    const string &description, utils::Verbosity verbosity)
    : SearchAlgorithm(cost_type, bound, max_time, description, verbosity),
      evaluator(h),
      preferred_operator_evaluators(preferred),
      preferred_usage(preferred_usage),
      lazy_evaluation(lazy),
      global_closed_list(global_closed),
      dead_end_pruning(dead_end),
      current_eval_context(state_registry.get_initial_state(), &statistics),
      current_phase_start_g(-1),
      num_ehc_phases(0),
      last_num_expanded(-1) {

    for (const shared_ptr<Evaluator> &eval : preferred_operator_evaluators) {
        eval->get_path_dependent_evaluators(path_dependent_evaluators);
    }
    evaluator->get_path_dependent_evaluators(path_dependent_evaluators);

    State initial_state = state_registry.get_initial_state();
    for (Evaluator *evaluator : path_dependent_evaluators) {
        evaluator->notify_initial_state(initial_state);
    }

    use_preferred = find(
                        preferred_operator_evaluators.begin(),
                        preferred_operator_evaluators.end(),
                        evaluator) != preferred_operator_evaluators.end();

    open_list =
        create_ehc_open_list_factory(verbosity, use_preferred, preferred_usage)
            ->create_edge_open_list();
}

void EnforcedHillClimbingSearch::reach_state(
    const State &parent, OperatorID op_id, const State &state) {
    for (Evaluator *evaluator : path_dependent_evaluators) {
        evaluator->notify_state_transition(parent, op_id, state);
    }
}

void EnforcedHillClimbingSearch::initialize() {
    assert(evaluator);
    log << "Conducting enforced hill-climbing search, (real) bound = " << bound
        << endl;
    if (use_preferred) {
        log << "Using preferred operators for "
            << (preferred_usage == PreferredUsage::RANK_PREFERRED_FIRST
                    ? "ranking successors"
                    : "pruning")
            << endl;
    }

    bool dead_end =
        current_eval_context.is_evaluator_value_infinite(evaluator.get());
    statistics.inc_evaluated_states();
    print_initial_evaluator_values(current_eval_context);

    if (dead_end) {
        log << "Initial state is a dead end, no solution" << endl;
        if (evaluator->dead_ends_are_reliable())
            utils::exit_with(ExitCode::SEARCH_UNSOLVABLE);
        else
            utils::exit_with(ExitCode::SEARCH_UNSOLVED_INCOMPLETE);
    }

    SearchNode node = search_space.get_node(current_eval_context.get_state());
    node.open_initial();

    current_phase_start_g = 0;
}

void EnforcedHillClimbingSearch::insert_successor_into_open_list(
    const EvaluationContext &eval_context, int parent_g, OperatorID op_id,
    bool preferred) {
    OperatorProxy op = task_proxy.get_operators()[op_id];
    int succ_g = parent_g + get_adjusted_cost(op);
    const State &state = eval_context.get_state();
    EdgeOpenListEntry entry = make_pair(state.get_id(), op_id);
    EvaluationContext new_eval_context(
        eval_context, succ_g, preferred, &statistics);
    open_list->insert(new_eval_context, entry);
    // } else {
    //     // Non-lazy heuristic evaluation implemented here (evaluate heuristic for successor state before inserting into open list)
    //     State succ_state = state_registry.get_successor_state(parent_state, op);
    //     StateID succ_state_id = succ_state.get_id();

    //     SearchNode parent_node = search_space.get_node(parent_state);
    //     SearchNode succ_node = search_space.get_node(succ_state);

    //     // Open the node and register the transition IF its a new state
    //     if (succ_node.is_new()) {
    //         succ_node.open_new_node(parent_node, op, get_adjusted_cost(op));
    //         reach_state(parent_state, op_id, succ_state);
    //     }

    //     EdgeOpenListEntry entry = make_pair(succ_state_id, op_id);
    //     EvaluationContext new_eval_context(succ_state, succ_g, preferred, &statistics);
    //     new_eval_context.get_evaluator_value(evaluator.get()); // inserts the child nodes heuristic value
    //     // reach_state(parent_state, op_id, succ_state);
    //     open_list->insert(new_eval_context, entry);
    // }

    statistics.inc_generated_ops();
}

void EnforcedHillClimbingSearch::expand(EvaluationContext &eval_context) {
    SearchNode node = search_space.get_node(eval_context.get_state());
    int node_g = node.get_g();

    ordered_set::OrderedSet<OperatorID> preferred_operators;
    if (use_preferred) {
        for (const shared_ptr<Evaluator> &preferred_operator_evaluator :
             preferred_operator_evaluators) {
            collect_preferred_operators(
                eval_context, preferred_operator_evaluator.get(),
                preferred_operators);
        }
    }

    if (use_preferred &&
        preferred_usage == PreferredUsage::PRUNE_BY_PREFERRED) {
        for (OperatorID op_id : preferred_operators) {
            insert_successor_into_open_list(eval_context, node_g, op_id, true);
        }
    } else {
        /* The successor ranking implied by RANK_BY_PREFERRED is done
           by the open list. */
        vector<OperatorID> successor_operators;
        successor_generator.generate_applicable_ops(
            eval_context.get_state(), successor_operators);
        for (OperatorID op_id : successor_operators) {
            bool preferred =
                use_preferred && preferred_operators.contains(op_id);
            insert_successor_into_open_list(
                eval_context, node_g, op_id, preferred);
        }
    }

    statistics.inc_expanded();
    node.close();
}

void EnforcedHillClimbingSearch::expand_nolazy(EvaluationContext &eval_context) {
    SearchNode node = search_space.get_node(eval_context.get_state());
    int node_g = node.get_g();

    ordered_set::OrderedSet<OperatorID> preferred_operators;
    if (use_preferred) {
        for (const shared_ptr<Evaluator> &preferred_operator_evaluator :
             preferred_operator_evaluators) {
            collect_preferred_operators(
                eval_context, preferred_operator_evaluator.get(),
                preferred_operators);
        }
    }

    if (use_preferred &&
        preferred_usage == PreferredUsage::PRUNE_BY_PREFERRED) {
        for (OperatorID op_id : preferred_operators) {
            OperatorProxy op = task_proxy.get_operators()[op_id];
            State succ_state = state_registry.get_successor_state(eval_context.get_state(), op);

            if (dead_end_pruning) {
                if (dead_end_list.find(succ_state.get_id()) != dead_end_list.end())
                    continue;
            }

            SearchNode succ_node = search_space.get_node(succ_state);
            // skip non-new states being added entirely to avoid duplicate entries
            // if (!succ_node.is_new())
            //     continue;
            if (succ_node.is_new()) {
                succ_node.open_new_node(node, op, get_adjusted_cost(op));
                reach_state(eval_context.get_state(), op_id, succ_state);
            }

            int succ_g = node_g + get_adjusted_cost(op);
            EvaluationContext succ_eval_context(succ_state, succ_g, true, &statistics);
            succ_eval_context.get_evaluator_value(evaluator.get());

            EdgeOpenListEntry entry(succ_state.get_id(), op_id);

            // not new nodes still get to the open list but get pruned later anyway, a little inconsistent
            open_list->insert(succ_eval_context, entry);
            statistics.inc_generated_ops();
        }


    } else {

        vector<OperatorID> successor_operators;
        successor_generator.generate_applicable_ops(eval_context.get_state(), successor_operators);

        for (OperatorID op_id : successor_operators) {
            bool preferred = use_preferred && preferred_operators.contains(op_id);
            OperatorProxy op = task_proxy.get_operators()[op_id];
            State succ_state = state_registry.get_successor_state(eval_context.get_state(), op);

            if (dead_end_pruning) {
                if (dead_end_list.find(succ_state.get_id()) != dead_end_list.end())
                    continue;
            }

            SearchNode succ_node = search_space.get_node(succ_state);

            // if (!succ_node.is_new())
            //     continue;
            if (succ_node.is_new()) {
                succ_node.open_new_node(node, op, get_adjusted_cost(op));
                reach_state(eval_context.get_state(), op_id, succ_state);
            }
            
            int succ_g = node_g + get_adjusted_cost(op);
            EvaluationContext succ_eval_context(succ_state, succ_g, preferred, &statistics);

            // evaluate s'
            succ_eval_context.get_evaluator_value(evaluator.get());
            EdgeOpenListEntry entry(succ_state.get_id(), op_id);
            open_list->insert(succ_eval_context,entry);
            statistics.inc_generated_ops();
        }
    }

    statistics.inc_expanded();
    node.close();
}

SearchStatus EnforcedHillClimbingSearch::step() {
    last_num_expanded = statistics.get_expanded();
    search_progress.check_progress(current_eval_context);

    if (check_goal_and_set_plan(current_eval_context.get_state())) {
        return SOLVED;
    }

    if (lazy_evaluation) {
        expand(current_eval_context);
    } else {
        expand_nolazy(current_eval_context);
    }
    
    return ehc();
}

SearchStatus EnforcedHillClimbingSearch::ehc_lazy_loop() { 
    while (!open_list->empty()) {
        EdgeOpenListEntry entry = open_list->remove_min();
        StateID parent_state_id = entry.first;
        OperatorID last_op_id = entry.second;
        OperatorProxy last_op = task_proxy.get_operators()[last_op_id];

        State parent_state = state_registry.lookup_state(parent_state_id);
        SearchNode parent_node = search_space.get_node(parent_state);

        int d = parent_node.get_g() - current_phase_start_g +
                get_adjusted_cost(last_op);

        if (parent_node.get_real_g() + last_op.get_cost() >= bound)
            continue;
        
        State state = state_registry.get_successor_state(parent_state, last_op);
        statistics.inc_generated();

        SearchNode node = search_space.get_node(state);
        StateID state_id = state.get_id();

        if (dead_end_pruning) { // if dead_end is true, then prune them 
            if (dead_end_list.find(state_id) != dead_end_list.end())
                continue;
        }

        if (!global_closed_list) { // if local list
            if (local_closed_list.find(state_id) != local_closed_list.end()) // if state already in local closed list then skip if not then add 
                continue;
            local_closed_list.insert(state_id);
        }

        if (global_closed_list && !node.is_new())
            continue;

        // if (node.is_new()) { // was originally here but leads to no solution now
            EvaluationContext eval_context(state, &statistics);
            reach_state(parent_state, last_op_id, state);
            statistics.inc_evaluated_states();

            if (eval_context.is_evaluator_value_infinite(evaluator.get())) {
                node.mark_as_dead_end();
                if (dead_end_pruning) {
                    dead_end_list.insert(state_id); // insert dead end into dead end closed list
                }
                statistics.inc_dead_ends();
                continue;
            }

            int h = eval_context.get_evaluator_value(evaluator.get());
            if (node.is_new()) { // check here otherwise it never terminates
                node.open_new_node(parent_node, last_op, get_adjusted_cost(last_op));
            }
            if (h < current_eval_context.get_evaluator_value(evaluator.get())) {
                ++num_ehc_phases;
                if (d_counts.count(d) == 0) {
                    d_counts[d] = make_pair(0, 0);
                }
                pair<int, int> &d_pair = d_counts[d];
                d_pair.first += 1;
                d_pair.second += statistics.get_expanded() - last_num_expanded;

                current_eval_context = move(eval_context);
                open_list->clear();
                if (!global_closed_list) {
                    local_closed_list.clear();
                }
                current_phase_start_g = node.get_g();
                return IN_PROGRESS;
            } else {
                expand(eval_context);
            }
        // }
    }

    log << "No solution - FAILED" << endl;
    return FAILED;
}

SearchStatus EnforcedHillClimbingSearch::ehc_nolazy_loop() {
    while (!open_list->empty()) {
        // (successor_state_id, op_id)
        EdgeOpenListEntry entry = open_list->remove_min();
        StateID state_id = entry.first; // state = succ state
        OperatorID op_id = entry.second;
        OperatorProxy op = task_proxy.get_operators()[op_id];

        State state = state_registry.lookup_state(state_id);
        SearchNode node = search_space.get_node(state);

        int d = node.get_g() - current_phase_start_g;

        if (node.get_real_g() >= bound)
            continue;
        statistics.inc_generated();

        if (dead_end_pruning) { // if dead_end is true, then prune them 
            if (dead_end_list.find(state_id) != dead_end_list.end())
                continue;
        }

        if (!global_closed_list) { // if local list
            if (local_closed_list.find(state_id) != local_closed_list.end()) // if state already in local closed list then skip, if not insert into local closed list
                continue;
            local_closed_list.insert(state_id);
        }

        if (global_closed_list && !node.is_new())
            continue;

        EvaluationContext eval_context(state, node.get_g(), false, &statistics);
        // the reach_state here is moved into insert_successor_into_open_list because it contains the parent node and this not saved here in the edge open list
        statistics.inc_evaluated_states();
        

        if (eval_context.is_evaluator_value_infinite(evaluator.get())) {
            node.mark_as_dead_end();
            if (dead_end_pruning) {
                dead_end_list.insert(state_id); // insert dead end into dead end closed list
            }
            statistics.inc_dead_ends();
            continue;
        }

        int h = eval_context.get_evaluator_value(evaluator.get());
        // if (node.is_new()) { //checking this here leads to no solution being found when lazy=false
        //node.open_new_node(parent_node, op, get_adjusted_cost(op)); 

            if (h < current_eval_context.get_evaluator_value(evaluator.get())) {
                ++num_ehc_phases;
                if (d_counts.count(d) == 0) {
                    d_counts[d] = make_pair(0, 0);
                }
                pair<int, int> &d_pair = d_counts[d];
                d_pair.first += 1;
                d_pair.second += statistics.get_expanded() - last_num_expanded;

                current_eval_context = move(eval_context);
                open_list->clear();
                if (!global_closed_list) {
                    local_closed_list.clear();
                }
                current_phase_start_g = node.get_g();
                return IN_PROGRESS;
            } else {
                expand_nolazy(eval_context); // uses this new function for non lazy eval
            }
        //}
    }

    log << "No solution - FAILED" << endl;
    return FAILED;
}

SearchStatus EnforcedHillClimbingSearch::ehc() {
    if (lazy_evaluation) {
        return ehc_lazy_loop();
    } else {
        return ehc_nolazy_loop();
    }
}

void EnforcedHillClimbingSearch::print_statistics() const {
    statistics.print_detailed_statistics();

    log << "EHC phases: " << num_ehc_phases << endl;
    assert(num_ehc_phases != 0);
    log << "Average expansions per EHC phase: "
        << static_cast<double>(statistics.get_expanded()) / num_ehc_phases
        << endl;

    for (auto count : d_counts) {
        int depth = count.first;
        int phases = count.second.first;
        assert(phases != 0);
        int total_expansions = count.second.second;
        log << "EHC phases of depth " << depth << ": " << phases
            << " - Avg. Expansions: "
            << static_cast<double>(total_expansions) / phases << endl;
    }
}

class EnforcedHillClimbingSearchFeature
    : public plugins::TypedFeature<
          SearchAlgorithm, EnforcedHillClimbingSearch> {
public:
    EnforcedHillClimbingSearchFeature() : TypedFeature("ehc") {
        document_title("Lazy enforced hill-climbing");
        document_synopsis("");

        add_option<shared_ptr<Evaluator>>("h", "heuristic");
        add_option<PreferredUsage>(
            "preferred_usage", "preferred operator usage",
            "prune_by_preferred");
        add_list_option<shared_ptr<Evaluator>>(
            "preferred", "use preferred operators of these evaluators", "[]");
        add_option<bool>("lazy", "use lazy heuristic evaluation", "true");
        add_option<bool>("global_closed", "use global closed list", "true");
        add_option<bool>("dead_end", "use dead-end pruning", "false"); // using dead-end pruning false by default
        add_search_algorithm_options_to_feature(*this, "ehc");
    }

    virtual shared_ptr<EnforcedHillClimbingSearch> create_component(
        const plugins::Options &opts) const override {
        return plugins::make_shared_from_arg_tuples<EnforcedHillClimbingSearch>(
            opts.get<shared_ptr<Evaluator>>("h"),
            opts.get<PreferredUsage>("preferred_usage"),
            opts.get_list<shared_ptr<Evaluator>>("preferred"),
            opts.get<bool>("lazy"),
            opts.get<bool>("global_closed"),
            opts.get<bool>("dead_end"),
            get_search_algorithm_arguments_from_options(opts));
    }
};

static plugins::FeaturePlugin<EnforcedHillClimbingSearchFeature> _plugin;

static plugins::TypedEnumPlugin<PreferredUsage> _enum_plugin(
    {{"prune_by_preferred",
      "prune successors achieved by non-preferred operators"},
     {"rank_preferred_first",
      "first insert successors achieved by preferred operators, "
      "then those by non-preferred operators"}});
}
