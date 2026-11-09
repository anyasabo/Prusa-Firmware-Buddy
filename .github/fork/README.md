# Fork anyasabo/Prusa-Firmware-Buddy: branches and releases

## Branches

| Branch | What | Rules |
| --- | --- | --- |
| `master` | Follows `prusa3d/Prusa-Firmware-Buddy` master | Never push to it |
| Topic branches (`eth-rx-descriptors`, `thermal-runaway-gap-pr`, `fanctl-pwm-on-int16`, ...) | One upstream pull request each, on `upstream/master` | No fork files. No test G-codes or runtime switches. Do not force-push. To change history, make a new branch (`-pr`, `-v2`) |
| `metrics-defaults` | The experiment line: upload fixes behind runtime switches (on at boot), FatFs R0.16, USB host 3.5.5, metrics on by default, profiling, test G-codes `M9958` and `M9959` | Experiments only. Each experiment branch starts from it |
| `anya/main` | The build that people use: `upstream/master` + a merge of `metrics-defaults` + a merge of each topic branch + the fork files | Merge, do not rebase. Only the fork files and small fork fixes are commits of their own |

To update `anya/main`: merge the new upstream base (or a new upstream release tag), then merge each
topic branch and `metrics-defaults` again. A topic branch that upstream accepted is not merged again.
When the upload fixes get clean topic branches, they replace the merge of `metrics-defaults`.

## Build

One script builds `anya/main` or any topic branch worktree (a topic branch has no fork files):

```sh
flock /workspaces/prusa/.heavy.lock .github/fork/build.sh . anya.1
flock /workspaces/prusa/.heavy.lock .github/fork/build.sh ../Prusa-Firmware-Buddy-eth pr.eth-rx-descriptors.1
```

The output is in `build/package/` of this tree. The version stays upstream (9.0.0). The label goes
after the build number: `9.0.0+<commits>.anya.1`.

## Release

1. Tag the commit that was built, and push the tag: `v9.0.0-anya.1` for `anya/main`,
   `v9.0.0-pr.<branch>.1` for a topic branch.
2. `.github/fork/upload-release.sh TAG LABEL NOTES_FILE`. Notes: `RELEASE_NOTES.md` for `anya/main`,
   `notes/<branch>.md` for a topic branch. Each release is a pre-release.

GitHub Actions are off on this fork. Before you turn them on, disable the upstream workflows: they
call private Prusa workflows.
