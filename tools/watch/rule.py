#!/usr/bin/env python3
"""What rule GitHub has on `main`, read back, and whether it is the one asked for.

    python3 tools/watch/rule.py              asks GitHub, prints what is there and what to change
    python3 tools/watch/rule.py --self-test  the verdict on made-up answers; needs no network

Status 0: a rule that wants the two checks is in force, and nothing in it
stops a merge or a lot. 1: no rule at all. 2: GitHub could not be asked.
3: there is a rule, with something to change; each such thing is a line that
begins `WRONG:` and says what to click.

Why it is here. Nothing in the repository can stop `gh pr merge` on a pull
request with no CI run (#449, 25 November 2026: merged 59 seconds after it
was opened). A rule of GitHub's on `main` can, and only the person can set
one: every session is the person's login, and GitHub refuses a session the
rule's settings ("Resource not accessible by integration"). On 28 November
the person began to set it and found other options on the page than the ones
they had been told: GitHub has two forms, the ruleset and the classic branch
protection rule, and the ruleset is the one it leads to. What a session can
do is read the result back (anyone who can read the repository can read its
rulesets) and say, in the words of the form, what would stop every merge.

What is asked for (the ruleset form; GitHub Docs, "Creating rulesets for a
repository" and "Available rules for rulesets", read on 28 November 2026):

    Settings, Rules, Rulesets, New ruleset, New branch ruleset
    Enforcement status: Active (it starts as Disabled)
    Bypass list: empty
    Target branches: Add target, Include default branch
    Restrict deletions, Block force pushes: ticked already, left so
    Require status checks to pass: ticked, with the checks
        Build & unit tests
        Documentation shape
      and "Require branches to be up to date before merging" NOT ticked
    everything else not ticked

Both checks are jobs of the short run that every push to a pull request
gets (.github/workflows/ci.yml), so every pushed head has them. A check that
the short run skips must not be required: no pull request would have it.
"Up to date" must be off because a lot lands several pull requests one after
the other, each as it is: after the first, the rest are behind `main`.
"""

from __future__ import annotations

import json
import subprocess
import sys

REPO = "repos/{owner}/{repo}"      # `gh api` fills the two in from the clone
WANTED = {"Build & unit tests", "Documentation shape"}
MAIN = ("~DEFAULT_BRANCH", "refs/heads/main")
ASKED = ("deletion", "non_fast_forward", "required_status_checks")    # the three kinds of rule the form ends with
ACTIVE = ("active", "enabled")      # the API's word is "active"; its documentation's example of the list says "enabled"

# Rules of a ruleset that are not asked for, and what each would do here.
NOT_ASKED = {
    "update": "\"Restrict updates\" is ticked: only the bypass list may change main, and the list is to be empty, "
              "so no pull request could be merged. Untick it",
    "creation": "\"Restrict creations\" is ticked: it does nothing to main, which exists. Untick it all the same",
    "required_linear_history": "\"Require linear history\" is ticked: merge commits are refused, and every pull request "
                               "here lands as a merge commit. Untick it",
    "required_signatures": "\"Require signed commits\" is ticked: the sessions' commits are not signed, so no pull "
                           "request could be merged. Untick it",
    "required_deployments": "\"Require deployments to succeed\" is ticked: nothing here deploys, so no pull request "
                            "could be merged. Untick it",
    "merge_queue": "\"Require merge queue\" is on: a lot is merged one pull request after the other, not through a "
                   "queue. Take it off",
    "code_scanning": "\"Require code scanning results\" is ticked: no code scanning is set up here, and GitHub then "
                     "refuses every merge. Untick it",
    "code_quality": "\"Require code quality results\" is ticked: none is set up here. Untick it",
    "copilot_code_review": "\"Automatically request Copilot code review\" is ticked: it stops nothing, and nobody "
                           "here reads a review. Untick it",
}
# The names of the kinds and of their parameters are those of GitHub's REST
# API ("REST API endpoints for rules", read on 28 November 2026): creation,
# update, deletion, required_linear_history, merge_queue,
# required_deployments, required_signatures, pull_request,
# required_status_checks, non_fast_forward and the patterns. The last three
# above are not in the part of that page that was read.
PULL_REQUEST_ASKS = {
    "required_approving_review_count": "approvals",
    "required_reviewers": "the review of a named team",
    "dismiss_stale_reviews_on_push": "stale approvals dismissed",
    "require_code_owner_review": "a code owner's review",
    "require_last_push_approval": "approval of the last push",
    "required_review_thread_resolution": "every conversation resolved",
}


def rule_faults(called: str, rule: dict) -> tuple[list[str], set[str] | None]:
    """(what to change in one rule of a ruleset, the checks it requires when it is the rule of the status checks)."""
    faults: list[str] = []
    kind, parameters = rule.get("type"), rule.get("parameters") or {}
    names: set[str] | None = None
    if kind == "required_status_checks":
        names = {str(check.get("context")) for check in parameters.get("required_status_checks") or [] if isinstance(check, dict)}
        missing, extra = sorted(WANTED - names), sorted(names - WANTED)
        if missing:
            faults.append(f"{called}: of the two checks, {' and '.join(missing)} {'is' if len(missing) == 1 else 'are'} "
                          "not required. Under \"Require status checks to pass\": Add checks, type the name, pick it")
        if extra:
            faults.append(f"{called} requires {', '.join(extra)} as well: not asked for, and a check that the short run "
                          "of a pull request skips would stop every merge. Take it out of the list")
        if parameters.get("strict_required_status_checks_policy"):
            faults.append(f"{called}: \"Require branches to be up to date before merging\" is ticked. Untick it: "
                          "with it on, only the first pull request of a lot can land")
    if kind in NOT_ASKED:
        faults.append(f"{called}: {NOT_ASKED[kind]}")
    elif kind not in ASKED and kind != "pull_request":
        # A kind of rule GitHub has and this does not know: said, not passed over.
        faults.append(f"{called} has a rule of the kind `{kind}`: not asked for, and what it would stop is not known "
                      "here. Untick it")
    if kind == "pull_request":
        asks = [words for key, words in PULL_REQUEST_ASKS.items() if parameters.get(key)]
        methods = parameters.get("allowed_merge_methods")
        if asks:
            faults.append(f"{called}: \"Require a pull request before merging\" is ticked and asks for "
                          f"{', '.join(asks)}: every session is one login, and nobody can approve their own pull "
                          "request. Untick the rule")
        if methods is not None and "merge" not in methods:
            faults.append(f"{called}: \"Allowed merge methods\" is {methods}, without merge: every pull request "
                          "here lands as a merge commit. Untick \"Require a pull request before merging\"")
    return faults, names


def verdict(branch: dict | None, rulesets: list | None, details: dict, applied: list | None) -> tuple[list[str], list[str], bool]:
    """(what is there, what to change, whether a rule that wants the two checks is in force on main).

    `branch` is `gh api repos/.../branches/main`, `rulesets` the list of
    `.../rulesets`, `details` each ruleset by its id as `.../rulesets/ID`
    gives it (the list leaves the rules and the targets out), and `applied`
    is `.../rules/branches/main`: the rules of active rulesets that hold
    main, each with the id of its ruleset. A ruleset whose details could
    not be read is judged by its rules in `applied` alone, and nothing is
    said of its targets: not knowing is not a fault of the person's.
    """
    there: list[str] = []
    wrong: list[str] = []
    in_force = False
    branch = branch if isinstance(branch, dict) else {}
    classic = ((branch.get("protection") or {}).get("required_status_checks") or {})
    classic_names = {str(name) for name in classic.get("contexts") or []}
    if branch.get("protected"):
        there.append("GitHub calls main protected (it says so for a ruleset as for a classic rule); the classic rule's "
                     f"checks: {sorted(classic_names) or 'none'}, enforced for: {classic.get('enforcement_level') or 'nobody'}")
    read: set = set()
    for listed in rulesets or []:
        if not isinstance(listed, dict):
            continue
        detail = details.get(listed.get("id"))
        if not isinstance(detail, dict):
            there.append(f"the ruleset \"{listed.get('name')}\" (id {listed.get('id')}): {listed.get('enforcement')}; "
                         "its rules and targets could not be read")
            continue
        read.add(listed.get("id"))
        name, enforcement = detail.get("name"), detail.get("enforcement")
        called = f"the ruleset \"{name}\""
        ref_name = ((detail.get("conditions") or {}).get("ref_name") or {})
        include, exclude = list(ref_name.get("include") or []), list(ref_name.get("exclude") or [])
        rules = [rule for rule in detail.get("rules") or [] if isinstance(rule, dict)]
        there.append(f"{called} (id {listed.get('id')}): {enforcement}; a {detail.get('target')} ruleset; targets {include or 'nothing'}"
                     + (f", but not {exclude}" if exclude else "")
                     + f"; rules: {', '.join(str(rule.get('type')) for rule in rules) or 'none'}"
                     + f"; bypass list: {detail.get('bypass_actors') if detail.get('bypass_actors') is not None else 'not shown'}"
                     + f"; this login can bypass: {detail.get('current_user_can_bypass', 'not shown')}")
        if detail.get("target") != "branch":
            wrong.append(f"{called} is a {detail.get('target')} ruleset. What is asked for is a branch ruleset "
                         "(New ruleset, New branch ruleset)")
            continue
        on_main = any(pattern in MAIN or pattern == "~ALL" for pattern in include) and not any(p in MAIN for p in exclude)
        active = enforcement in ACTIVE
        if not active:
            wrong.append(f"{called}: \"Enforcement status\" is {enforcement}, so it does nothing. "
                         "Set it to Active, and click Save changes at the bottom")
        if not on_main:
            wrong.append(f"{called} does not target main. Under \"Target branches\": Add target, Include default branch")
        if any(pattern not in MAIN for pattern in include):
            wrong.append(f"{called} targets more than main ({include}): its rules would hold every agent's own branch, "
                         "where the first push has no CI run yet and the landing session pushes with force. "
                         "Leave \"Include default branch\" as the only target")
        if not any(rule.get("type") == "required_status_checks" for rule in rules):
            wrong.append(f"{called}: \"Require status checks to pass\" is not ticked, so it asks for no CI run. "
                         f"Tick it, and under it Add checks: {', '.join(sorted(WANTED))}")
        for rule in rules:
            faults, names = rule_faults(called, rule)
            wrong += faults
            if names is not None and names >= WANTED and on_main and active:
                in_force = True
        if detail.get("bypass_actors"):
            wrong.append(f"{called}: the bypass list is not empty. Every session is your login, so whoever of your "
                         "roles is on it, every session is on it. Remove the entries")
        elif detail.get("current_user_can_bypass") not in (None, "never"):
            wrong.append(f"{called}: GitHub says this login can bypass it ({detail.get('current_user_can_bypass')}). "
                         "Every session is this login. Empty the bypass list")
    if classic_names:
        level = classic.get("enforcement_level")
        if classic_names >= WANTED and level not in (None, "off"):
            in_force = True
        if classic_names != WANTED:
            wrong.append(f"the classic rule requires {sorted(classic_names)}; asked for: {sorted(WANTED)}")
        if level == "non_admins":
            wrong.append("the classic rule does not hold for administrators, and every session is your login, an "
                         "administrator: tick \"Do not allow bypassing the above settings\"")
    held = [rule for rule in applied or [] if isinstance(rule, dict)]
    for rule in held:
        # What holds main, whatever could be read of its ruleset: the last word on "in force".
        faults, names = rule_faults(f"a rule that holds main (ruleset {rule.get('ruleset_id')})", rule)
        if rule.get("ruleset_id") not in read:
            wrong += faults
        if names is not None and names >= WANTED:
            in_force = True
    there.append("rules GitHub applies to main now: " + (", ".join(str(rule.get("type")) for rule in held) or "none"))
    return there, wrong, in_force


def ask(path: str):
    try:
        done = subprocess.run(("gh", "api", f"{REPO}/{path}"), capture_output=True, text=True, timeout=40)
        return json.loads(done.stdout) if done.returncode == 0 else None
    except (OSError, ValueError, subprocess.SubprocessError):
        return None


def report() -> int:
    branch, rulesets, applied = ask("branches/main"), ask("rulesets"), ask("rules/branches/main")
    if not isinstance(branch, dict) or not isinstance(rulesets, list):
        print("GitHub could not be asked (gh api repos/.../branches/main, .../rulesets): no answer, which is not \"no rule\"")
        return 2
    if not (branch.get("protected") or rulesets or applied):
        print("no rule on main: GitHub says it is not protected, and the repository has no ruleset")
        return 1
    details = {listed.get("id"): ask(f"rulesets/{listed.get('id')}") for listed in rulesets if isinstance(listed, dict)}
    unread = [str(key) for key, value in details.items() if not isinstance(value, dict)]
    there, wrong, in_force = verdict(branch, rulesets, {k: v for k, v in details.items() if isinstance(v, dict)}, applied)
    for line in there:
        print(line)
    for line in wrong:
        print(f"WRONG: {line}")
    if unread:
        print(f"the rules of ruleset {', '.join(unread)} could not be read: what is said above of it is from the list alone")
    if in_force and not wrong:
        if unread:
            print("\nin force: both checks are required on main. What else the ruleset holds could not be read")
            return 3
        print("\nin force, and as asked for: a head commit is merged into main only when both checks have passed on it")
        return 0
    print("\nin force, with the above to change" if in_force else "\nNOT in force: nothing stops a merge with no CI run yet")
    return 3


def self_test() -> int:
    def ruleset(more: list | None = None, **changes) -> dict:
        """The ruleset as asked for, with `more` rules added, or with keys changed (`rules` among them)."""
        made = {"id": 1, "name": "main needs green CI", "target": "branch", "enforcement": "active", "bypass_actors": [],
                "current_user_can_bypass": "never",
                "conditions": {"ref_name": {"include": ["~DEFAULT_BRANCH"], "exclude": []}},
                "rules": [{"type": "deletion"}, {"type": "non_fast_forward"}, required(False, *sorted(WANTED))] + (more or [])}
        made.update(changes)
        return made

    def required(strict: bool, *names: str) -> dict:
        return {"type": "required_status_checks", "parameters": {
            "strict_required_status_checks_policy": strict,
            "required_status_checks": [{"context": name, "integration_id": 15368} for name in names]}}

    def asked(one: dict) -> tuple[int, bool]:
        _there, wrong, in_force = verdict({"protected": True, "protection": {"required_status_checks": {"contexts": [], "enforcement_level": "off"}}},
                                          [{"id": 1}], {1: one}, [])
        return len(wrong), in_force

    def classic(names: list[str], level: str) -> tuple[int, bool]:
        _there, wrong, in_force = verdict({"protected": bool(names), "protection": {"required_status_checks": {"contexts": names, "enforcement_level": level}}},
                                          [], {}, [])
        return len(wrong), in_force

    plain = [{"type": "deletion"}, {"type": "non_fast_forward"}]
    cases = [
        ("the ruleset as asked for: in force, nothing to change", asked(ruleset()), (0, True)),
        ("left on Disabled: it does nothing", asked(ruleset(enforcement="disabled")), (1, False)),
        ("no target: it holds no branch", asked(ruleset(conditions={"ref_name": {"include": [], "exclude": []}})), (1, False)),
        ("every branch: in force, and it would hold the agents' branches too",
         asked(ruleset(conditions={"ref_name": {"include": ["~ALL"], "exclude": []}})), (1, True)),
        ("main by its name is main", asked(ruleset(conditions={"ref_name": {"include": ["refs/heads/main"], "exclude": []}})), (0, True)),
        ("\"up to date\" ticked: in force, and a lot could not land", asked(ruleset(rules=plain + [required(True, *sorted(WANTED))])), (1, True)),
        ("one of the two checks: not the rule asked for", asked(ruleset(rules=plain + [required(False, "Build & unit tests")])), (1, False)),
        ("a third check, one the short run skips", asked(ruleset(rules=plain + [required(False, *sorted(WANTED), "Sanitizers: fuzzing")])), (1, True)),
        ("no status checks at all: it asks for no run", asked(ruleset(rules=plain)), (1, False)),
        ("\"Restrict updates\": nothing could be merged", asked(ruleset([{"type": "update"}])), (1, True)),
        ("a pull request with one approval: nobody can give it",
         asked(ruleset([{"type": "pull_request", "parameters": {"required_approving_review_count": 1}}])), (1, True)),
        ("a pull request with no approval, any merge method: harmless",
         asked(ruleset([{"type": "pull_request", "parameters": {"required_approving_review_count": 0,
                                                              "allowed_merge_methods": ["merge", "squash", "rebase"]}}])), (0, True)),
        ("squash only: no merge commit could land",
         asked(ruleset([{"type": "pull_request", "parameters": {"required_approving_review_count": 0, "allowed_merge_methods": ["squash"]}}])), (1, True)),
        ("linear history: the same", asked(ruleset([{"type": "required_linear_history"}])), (1, True)),
        ("signed commits", asked(ruleset([{"type": "required_signatures"}])), (1, True)),
        ("somebody on the bypass list", asked(ruleset(bypass_actors=[{"actor_type": "RepositoryRole", "actor_id": 5}])), (1, True)),
        ("the list is not shown, and GitHub says this login can bypass",
         asked({key: value for key, value in ruleset(current_user_can_bypass="always").items() if key != "bypass_actors"}), (1, True)),
        ("a tag ruleset is not a rule on main", asked(ruleset(target="tag")), (1, False)),
        ("\"enabled\" for \"active\", as the documentation's example has it", asked(ruleset(enforcement="enabled")), (0, True)),
        ("\"evaluate\" enforces nothing", asked(ruleset(enforcement="evaluate")), (1, False)),
        ("a kind of rule this does not know is said, not passed over",
         asked(ruleset([{"type": "commit_message_pattern", "parameters": {"operator": "starts_with", "pattern": "x"}}])), (1, True)),
        ("the review of a named team: nobody can give it",
         asked(ruleset([{"type": "pull_request", "parameters": {"required_approving_review_count": 0,
                                                              "required_reviewers": [{"minimum_approvals": 1}]}}])), (1, True)),
        ("no rule at all", classic([], "off"), (0, False)),
        ("a classic rule as asked for", classic(sorted(WANTED), "everyone"), (0, True)),
        ("a classic rule that spares administrators: every session is one", classic(sorted(WANTED), "non_admins"), (1, True)),
        ("a classic rule with one check", classic(["Build & unit tests"], "everyone"), (1, False)),
        ("answers of another shape than expected are read as nothing, not as a crash",
         verdict(None, [None, "x"], {}, ["x", None])[1:], ([], False)),
        ("a ruleset whose rules could not be read: judged by what holds main, and no fault is made up",
         verdict({"protected": True}, [{"id": 1, "name": "main needs green CI", "enforcement": "active"}], {},
                 [dict(required(False, *sorted(WANTED)), ruleset_id=1), {"type": "deletion", "ruleset_id": 1}])[1:], ([], True)),
        ("...and \"up to date\" is still seen there",
         [len(found) if isinstance(found, list) else found for found in
          verdict({"protected": True}, [{"id": 1, "name": "main needs green CI", "enforcement": "active"}], {},
                  [dict(required(True, *sorted(WANTED)), ruleset_id=1)])[1:]], [1, True]),
        ("...a ruleset that is listed and holds nothing on main: not in force, and nothing made up",
         verdict({"protected": False}, [{"id": 1, "name": "main needs green CI", "enforcement": "disabled"}], {}, [])[1:], ([], False)),
    ]
    failed = 0
    for name, got, wanted in cases:
        ok = got == wanted
        failed += not ok
        print(f"  [{'ok  ' if ok else 'FAIL'}] {name}" + ("" if ok else f"\n         wanted {wanted}\n         got    {got}"))
    print(f"\n{len(cases) - failed}/{len(cases)} pass")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(self_test() if "--self-test" in sys.argv[1:] else report())
