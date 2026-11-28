# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the latest full CI runs on a candidate's tag before drafting its release."""

import json
import os
import subprocess
import sys
from urllib.parse import urlencode

REQUIRED_WORKFLOWS = ('regression', 'vst3', 'lv2', 'au', 'fuzz', 'lint')


def check_tag_ci(repository, tag, sha, *, rehearsal=False):
    query = urlencode({'branch': tag, 'head_sha': sha, 'event': 'workflow_dispatch', 'per_page': 100})
    problems = []
    for workflow in REQUIRED_WORKFLOWS:
        endpoint = f'repos/{repository}/actions/workflows/{workflow}.yml/runs?{query}'
        pages = json.loads(subprocess.check_output(['gh', 'api', '--paginate', '--slurp', endpoint], text=True))
        runs = (
            run
            for page in pages
            for run in page['workflow_runs']
            if run['event'] == 'workflow_dispatch' and run['head_branch'] == tag and run['head_sha'] == sha
        )
        latest = max(runs, key=lambda run: (run['run_number'], run['run_attempt']), default=None)
        if latest is None:
            problems.append(f'{workflow}: no manual run')
        elif latest['status'] != 'completed' or latest['conclusion'] != 'success':
            problems.append(
                f'{workflow}: run {latest["id"]} attempt {latest["run_attempt"]} '
                f'({latest["status"]}, {latest["conclusion"] or "no conclusion"})'
            )
    if problems:
        severity = 'warning' if rehearsal else 'error'
        print(
            f'::{severity}::Full CI on {tag} ({sha}): {"; ".join(problems)}. '
            'Wait for pending runs. For a failed run, rerun the listed run ID or start a new dispatch on the tag. '
            'Start a manual run on the tag for any missing workflow.'
        )
        return rehearsal
    print(f'The latest manual run of every required CI workflow passed on {tag} ({sha}).')
    return True


def check_candidate_run(repository, run_id):
    # This is the same immutable run ID that prepare_release checked against
    # the tag and source. A rerun can still change its latest result.
    endpoint = f'repos/{repository}/actions/runs/{run_id}'
    run = json.loads(subprocess.check_output(['gh', 'api', endpoint], text=True))
    if run['status'] == 'completed' and run['conclusion'] == 'success':
        return True
    print(f'::error::Candidate run {run_id} is {run["status"]} ({run["conclusion"] or "no conclusion"}).')
    return False


if __name__ == '__main__':
    tag, sha, run_id = sys.argv[1:]
    repository = os.environ['GITHUB_REPOSITORY']
    rehearsal = os.environ.get('GITHUB_EVENT_NAME') == 'pull_request'
    sys.exit(not (check_tag_ci(repository, tag, sha, rehearsal=rehearsal) and check_candidate_run(repository, run_id)))
