#! /usr/bin/env python3

import os
import itertools

from lab.environments import LocalEnvironment, BaselSlurmEnvironment

import common_setup
from common_setup import IssueConfig, IssueExperiment

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
        driver_options=['--search-time-limit', '5m', "--build", build])
    for build in BUILDS
    for config_nick, config in CONFIG_NICKS
]

SUITE = ["miconic:s1-0.pddl"] # = common_setup.DEFAULT_OPTIMAL_SUITE
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

exp.add_absolute_report_step()
exp.add_comparison_table_step()
exp.add_scatter_plot_step(relative=True, attributes=["total_time", "memory"])

# exp.add_archive_step(ARCHIVE_PATH)
# exp.add_archive_eval_dir_step(ARCHIVE_PATH)

exp.run_steps()