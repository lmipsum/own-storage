"""Refuses commits and pull requests that break the repository's rules.

Every commit but a merge is one of feat, fix, test, ci or build, names its
issue and no co-author; every commit carries the maintainer's identity. A
Dependabot build commit that GitHub made needs neither. Only the maintainer
contributes, so a pull request's author is checked once its commits land.
  python3 scripts/check-commits.py <git revision range>
  python3 scripts/check-commits.py --authors-only <git revision range>
  python3 scripts/check-commits.py --message - < message   (a pull request's
  title and description, which become the squashed commit)
"""

import re
import subprocess
import sys

SUMMARY = re.compile(r"^(feat|fix|test|ci|build): \S")
ISSUE = re.compile(r"^(Closes|Refs) #\d+", re.MULTILINE)
CO_AUTHOR = re.compile(r"^co-authored-by:", re.IGNORECASE | re.MULTILINE)
MAINTAINER = "26457258+lmipsum@users.noreply.github.com"
DEPENDABOT = "49699333+dependabot[bot]@users.noreply.github.com"
GITHUB = "noreply@github.com"


def message_problems(message):
    """The rules a commit message breaks, as short reasons."""
    problems = []
    if not SUMMARY.search(message.split("\n", 1)[0]):
        problems.append("summary is not 'feat|fix|test|ci|build: ...'")
    if not ISSUE.search(message):
        problems.append("no 'Closes #N' or 'Refs #N' line")
    if CO_AUTHOR.search(message):
        problems.append("names a co-author")
    return problems


def git(*arguments):
    """The output of a git command."""
    return subprocess.run(["git", *arguments], check=True, capture_output=True,
                          text=True).stdout


def commit_problems(commit, authors_only):
    """The rules a commit breaks; a merge only has to carry the right author."""
    author, committer, parents = git("show", "-s", "--format=%ae%n%ce%n%P", commit).split("\n")[:3]
    message = git("show", "-s", "--format=%B", commit)
    if author == DEPENDABOT and committer == GITHUB:
        return [] if SUMMARY.search(message) else ["a Dependabot commit that is not 'build: ...'"]
    problems = [] if author == MAINTAINER else [f"author is {author}"]
    if authors_only or len(parents.split()) > 1:
        return problems
    return problems + message_problems(message)


def main(arguments):
    if arguments[:2] == ["--message", "-"]:
        reports = {"the pull request": message_problems(sys.stdin.read())}
    else:
        authors_only = arguments[:1] == ["--authors-only"]
        revisions = arguments[1:] if authors_only else arguments
        try:
            commits = git("rev-list", *revisions).split()
            reports = {commit[:7]: commit_problems(commit, authors_only) for commit in commits}
        except subprocess.CalledProcessError as error:
            print(error.stderr.strip())
            return 1
    failed = False
    for name, problems in reports.items():
        for problem in problems:
            print(f"{name}: {problem}")
            failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
