# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the latest full CI runs on a candidate's tag before drafting its release."""

import json
import os
import subprocess
import sys
from urllib.parse import urlencode


def check_tag_ci(repository, tag, sha, *, rehearsal=False):
    query = urlencode({'branch': tag, 'head_sha': sha, 'event': 'workflow_dispatch', 'per_page': 100})
    problems = []
    for workflow in ('regression', 'vst3', 'lv2', 'au', 'fuzz', 'lint'):
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
            'Start or rerun the affected workflows on the tag and wait for them to pass.'
        )
        return rehearsal
    print(f'The latest manual run of every required CI workflow passed on {tag} ({sha}).')
    return True


if __name__ == '__main__':
    tag, sha = sys.argv[1:]
    sys.exit(
        not check_tag_ci(
            os.environ['GITHUB_REPOSITORY'], tag, sha, rehearsal=os.environ['GITHUB_EVENT_NAME'] == 'pull_request'
        )
    )
