#! /usr/bin/env python3

import os
import itertools

from lab.environments import LocalEnvironment, BaselSlurmEnvironment
from lab.reports import Report

import common_setup
from common_setup import IssueConfig, IssueExperiment
from downward.reports.absolute import AbsoluteReport

# ARCHIVE_PATH = "ai/downward/TODO"
DIR = os.path.dirname(os.path.abspath(__file__))
REPO_DIR = os.environ["DOWNWARD_REPO"]
BENCHMARKS_DIR = os.environ["DOWNWARD_BENCHMARKS"]
REVISIONS = ["HEAD"] # = ["release-22.12.0", "release-23.06.0"]
BUILDS = ["release"]

BOOLS = [True, False]
CONFIG_NICKS = []
for helpful_actions, lazy, global_closed, dead_end in itertools.product(
        BOOLS, BOOLS, BOOLS, BOOLS):

    nick = f"ehc-ff-l{int(lazy)}-gc{int(global_closed)}-de{int(dead_end)}-ha{int(helpful_actions)}"

    config = [
        "--search",
        # f"ehc(ff(), preferred=[ff()], "
        f"ehc(ff(helpful_actions={'true' if helpful_actions else 'false'}), preferred=[ff(helpful_actions={'true' if helpful_actions else 'false'})], "
        f"lazy={'true' if lazy else 'false'}, "
        f"global_closed={'true' if global_closed else 'false'}, "
        f"dead_end={'true' if dead_end else 'false'})"
    ]

    CONFIG_NICKS.append((nick, config))
CONFIGS = [
    IssueConfig(
        config_nick,
        config,
        build_options=[build],
        driver_options=['--search-time-limit', '30m', "--build", build,"--validate"])
    for build in BUILDS
    for config_nick, config in CONFIG_NICKS
]

SUITE = common_setup.DEFAULT_SATISFICING_SUITE #["miconic:s1-0.pddl"]
ENVIRONMENT = BaselSlurmEnvironment(
    partition="infai_2",
    email="eda.kaynar@stud.unibas.ch",
    memory_per_cpu="3947M",
    export=["PATH"],
)

if common_setup.is_test_run():
    SUITE = IssueExperiment.DEFAULT_TEST_SUITE
    ENVIRONMENT = LocalEnvironment(processes=4)

exp = IssueExperiment(
    REPO_DIR,
    revisions=REVISIONS,
    configs=CONFIGS,
    environment=ENVIRONMENT,
)
exp.add_suite(BENCHMARKS_DIR, SUITE)

exp.add_parser(exp.EXITCODE_PARSER)
exp.add_parser(exp.TRANSLATOR_PARSER)
exp.add_parser(exp.SINGLE_SEARCH_PARSER)
exp.add_parser(exp.PLANNER_PARSER)

exp.add_step('build', exp.build)
exp.add_step('start', exp.start_runs)
exp.add_step('parse', exp.parse)
exp.add_fetcher(name='fetch')

def filter_local_closed_list(run):
    return "-gc0-" in run["algorithm"]
    
def filter_global_closed_list(run):
    return "-gc1-" in run["algorithm"]

def filter_lazy(run):    
    return "-l1-" in run["algorithm"]

def filter_non_lazy(run):    
    return "-l0-" in run["algorithm"]

def filter_dead_end_pruning(run):    
    return "-de1-" in run["algorithm"]

def filter_no_dead_end_pruning(run):    
    return "-de0-" in run["algorithm"]

def filter_helpful_actions(run):    
    return "-ha1" in run["algorithm"]

def filter_preferred_ops(run):    
    return "-ha0" in run["algorithm"]

ATTRIBUTES=["cost", "coverage", "error", "evaluations", "expansions", "generated", 
     "initial_h_value", "memory", "planner_memory", "planner_time", "search_time" , 
     "total_time"]

first_report = AbsoluteReport(attributes=ATTRIBUTES, filter=[filter_local_closed_list]) // If too big then add more filters here
exp.add_report(first_report, outfile="local.html")

second_report = AbsoluteReport(attributes=ATTRIBUTES, filter=[filter_global_closed_list])
exp.add_report(second_report, outfile="global.html")

third_report = AbsoluteReport(attributes=ATTRIBUTES, filter=[filter_lazy])
exp.add_report(third_report, outfile="lazy.html")

fourth_report = AbsoluteReport(attributes=ATTRIBUTES, filter=[filter_non_lazy])
exp.add_report(fourth_report, outfile="non-lazy.html")

fifth_report = AbsoluteReport(attributes=ATTRIBUTES, filter=[filter_dead_end_pruning])
exp.add_report(fifth_report, outfile="dead-end.html")

sixth_report = AbsoluteReport(attributes=ATTRIBUTES, filter=[filter_no_dead_end_pruning])
exp.add_report(sixth_report, outfile="no-dead-end.html")

seventh_report = AbsoluteReport(attributes=ATTRIBUTES, filter=[filter_helpful_actions])
exp.add_report(seventh_report, outfile="helpful-actions.html")

eigth_report = AbsoluteReport(attributes=ATTRIBUTES, filter=[filter_preferred_ops])
exp.add_report(eigth_report, outfile="preferred-ops.html")

# exp.add_absolute_report_step(name="global", filter=filter_global_closed_list, 
# attributes=["cost", "coverage", "error", "evaluations", "expansions", "generated", 
#      "initial_h_value", "memory", "planner_memory", "planner_time", "search_time" , 
#      "total_time"])
# exp.add_absolute_report_step(name="lazy", filter=filter_lazy, 
# attributes=["cost", "coverage", "error", "evaluations", "expansions", "generated", 
#      "initial_h_value", "memory", "planner_memory", "planner_time", "search_time" , 
#      "total_time"])
# exp.add_absolute_report_step(name="non-lazy", filter=filter_non_lazy, 
# attributes=["cost", "coverage", "error", "evaluations", "expansions", "generated", 
#      "initial_h_value", "memory", "planner_memory", "planner_time", "search_time" , 
#      "total_time"])
# exp.add_absolute_report_step(name="dead-end-pruning", filter=filter_dead_end_pruning, 
# attributes=["cost", "coverage", "error", "evaluations", "expansions", "generated", 
#      "initial_h_value", "memory", "planner_memory", "planner_time", "search_time" , 
#      "total_time"])
# exp.add_absolute_report_step(name="no-dead-end-pruning", filter=filter_no_dead_end_pruning, 
# attributes=["cost", "coverage", "error", "evaluations", "expansions", "generated", 
#      "initial_h_value", "memory", "planner_memory", "planner_time", "search_time" , 
#      "total_time"])
# exp.add_absolute_report_step(name="helpful-actions", filter=filter_helpful_actions, 
# attributes=["cost", "coverage", "error", "evaluations", "expansions", "generated", 
#      "initial_h_value", "memory", "planner_memory", "planner_time", "search_time" , 
#      "total_time"])
# exp.add_absolute_report_step(name="preferred-ops", filter=filter_preferred_ops, 
# attributes=["cost", "coverage", "error", "evaluations", "expansions", "generated", 
#      "initial_h_value", "memory", "planner_memory", "planner_time", "search_time" , 
#      "total_time"])


# TODO: also run everything in one big summary table but generated results should be in .tex and not html format
exp.add_comparison_table_step()
exp.add_scatter_plot_step(relative=True, attributes=["total_time", "memory"])

# exp.add_archive_step(ARCHIVE_PATH)
# exp.add_archive_eval_dir_step(ARCHIVE_PATH)

exp.run_steps()