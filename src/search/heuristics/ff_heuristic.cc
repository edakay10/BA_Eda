#include "ff_heuristic.h"

#include "../plugins/plugin.h"
#include "../task_utils/task_properties.h"
#include "../utils/logging.h"

#include <cassert>

using namespace std;

namespace ff_heuristic {
// construction and destruction
FFHeuristic::FFHeuristic(
    tasks::AxiomHandlingType axioms, const shared_ptr<AbstractTask> &transform,
    bool cache_estimates, const string &description, utils::Verbosity verbosity, bool helpful_actions)
    : AdditiveHeuristic(
          axioms, transform, cache_estimates, description, verbosity),
      relaxed_plan(task_proxy.get_operators().size(), false),
      relaxed_plan_facts(task_properties::get_num_facts(task_proxy), false), 
      use_helpful_actions(helpful_actions) {
    if (log.is_at_least_normal()) {
        log << "Initializing FF heuristic..." << endl;
        log << "Using "
            << (helpful_actions ? "helpful actions" : "standard preferred operators")
            << endl;
    }
}

void FFHeuristic::mark_preferred_operators_and_relaxed_plan(
    const State &state, PropID goal_id) {
    Proposition *goal = get_proposition(goal_id);
    if (!goal->marked) { // Only consider each subgoal once.
        goal->marked = true;
        OpID op_id = goal->reached_by;
        if (op_id != NO_OP) { // We have not yet chained back to a start node.
            UnaryOperator *unary_op = get_operator(op_id);
            bool is_preferred = true;
            for (PropID precond : get_preconditions(op_id)) {
                mark_preferred_operators_and_relaxed_plan(state, precond);
                if (get_proposition(precond)->reached_by != NO_OP) {
                    is_preferred = false;
                }
            }
            int operator_no = unary_op->operator_no;
            if (operator_no != -1) {
                // This is not an axiom.
                relaxed_plan[operator_no] = true;
                OperatorProxy op = task_proxy.get_operators()[operator_no];
                for (EffectProxy eff : op.get_effects()) {
                    PropID eff_id = get_prop_id(eff.get_fact());
                    relaxed_plan_facts[eff_id] = true;
                }

                if (!use_helpful_actions && is_preferred) {
                    OperatorProxy op = task_proxy.get_operators()[operator_no];
                    assert(task_properties::is_applicable(op, state));
                    set_preferred(op);
                }
            }
        }
    }
}

void FFHeuristic::compute_helpful_actions(const State &state) {
    // Loop over ALL applicable operators in current state
    for (OperatorProxy op : task_proxy.get_operators()) {

        if (!task_properties::is_applicable(op, state))
            continue;

        // int op_no = op.get_id(); // or operator index mapping depending on your framework
        bool is_helpful = false;

        // Check ALL effects (not just best achiever chain)
        for (EffectProxy eff : op.get_effects()) {
            PropID eff_id = get_prop_id(eff.get_fact());
            // Proposition *prop = get_proposition(eff_id);

            // MUST check relaxed plan marking, not reached_by
            if (relaxed_plan_facts[eff_id]) {
                is_helpful = true;
                break;
            }
        }

        if (is_helpful) {
            set_preferred(op);
        }
    }
}

int FFHeuristic::compute_heuristic(const State &ancestor_state) {
    State state = convert_ancestor_state(ancestor_state);
    int h_add = compute_add_and_ff(state);
    if (h_add == DEAD_END)
        return h_add;

    // reset structs
    fill(relaxed_plan.begin(), relaxed_plan.end(), false);
    fill(relaxed_plan_facts.begin(), relaxed_plan_facts.end(), false);

    // Collecting the relaxed plan also sets the preferred operators.
    for (PropID goal_id : goal_propositions)
        mark_preferred_operators_and_relaxed_plan(state, goal_id);

    if (use_helpful_actions)
        compute_helpful_actions(state);

    int h_ff = 0;
    for (size_t op_no = 0; op_no < relaxed_plan.size(); ++op_no) {
        if (relaxed_plan[op_no]) {
            relaxed_plan[op_no] = false; // Clean up for next computation.
            h_ff += task_proxy.get_operators()[op_no].get_cost();
        }
    }
    return h_ff;
}

class FFHeuristicFeature
    : public plugins::TypedFeature<Evaluator, FFHeuristic> {
public:
    FFHeuristicFeature() : TypedFeature("ff") {
        document_title("FF heuristic with optional helpful actions");

        relaxation_heuristic::add_relaxation_heuristic_options_to_feature(
            *this, "ff");

        add_option<bool>("helpful_actions", "Use helpful actions derived from relaxed plan", "false"); // default uses preferred_ops

        document_language_support("action costs", "supported");
        document_language_support("conditional effects", "supported");
        document_language_support("axioms", "supported");

        document_property("admissible", "no");
        document_property("consistent", "no");
        document_property("safe", "yes");
        document_property("preferred operators", "yes");
    }

    virtual shared_ptr<FFHeuristic> create_component(
        const plugins::Options &opts) const override {
        return plugins::make_shared_from_arg_tuples<FFHeuristic>(
            relaxation_heuristic::
                get_relaxation_heuristic_arguments_from_options(opts),
            opts.get<bool>("helpful_actions"));
    }
};

static plugins::FeaturePlugin<FFHeuristicFeature> _plugin;
}
