# Fork anyasabo/Prusa-Firmware-Buddy: branches and releases

## Branches

| Branch | What | Rules |
| --- | --- | --- |
| `master` | Follows `prusa3d/Prusa-Firmware-Buddy` master | Never push to it |
| Topic branches (`eth-rx-descriptors`, `thermal-runaway-gap-pr`, `fanctl-pwm-on-int16`, ...) | One upstream pull request each, on `upstream/master` | No fork files. No test G-codes or runtime switches. Do not force-push. To change history, make a new branch (`-pr`, `-v2`) |
| `metrics-defaults` | The experiment line: upload fixes behind runtime switches (on at boot), FatFs R0.16, USB host 3.5.5, metrics on by default, profiling, test G-codes `M9958` and `M9959` | Experiments only. Each experiment branch starts from it |
| `anya/main` | The build that people use: `upstream/master` + a merge of `metrics-defaults` + a merge of each topic branch + the fork files | Merge, do not rebase. Only the fork files and small fork fixes are commits of their own |
| `anya/latest` | Before anya.8: `anya/main` + the GCC 16 toolchain. `anya/main` has that toolchain now | Not maintained. Kept for the old builds `latest.N` |

## Toolchain

`anya/main` uses the latest toolchain, not the pins of upstream. Thus the full fork finds the problems of a new compiler early. The toolchain comes from the branch `toolchain-gcc16-fork`:

- GCC 16.2.0, binutils 2.47 and newlib 4.6.0: the Arch Linux `arm-none-eabi-*` packages, copied unchanged to the fork release `toolchain-arm-none-eabi-gcc-16.2.0-arch.1`. `utils/bootstrap.py` checks the SHA-256 of each package.
- CMake 4.4.5 and Ninja 1.13.2.

The topic branches for upstream keep the pinned upstream toolchain (Arm GCC 13.3.1, CMake 3.28.3, Ninja 1.10.2). Before you file a pull request, build the topic branch with that toolchain. To change the toolchain of `anya/main`, change `toolchain-gcc16-fork` and merge it again. `lib/sbom.yml` must describe the new versions, else `utils/sbom/ci.sh check` fails.

## Update

To update `anya/main`: merge the new upstream base (or a new upstream release tag), then merge each
topic branch and `metrics-defaults` again. A topic branch that upstream accepted is not merged again.
When the upload fixes get clean topic branches, they replace the merge of `metrics-defaults`.

## Build

One script builds `anya/main` or any topic branch worktree (a topic branch has no fork files):

```sh
flock /workspaces/prusa/.heavy.lock .github/fork/build.sh . anya.2
flock /workspaces/prusa/.heavy.lock .github/fork/build.sh ../Prusa-Firmware-Buddy-eth pr.eth-rx-descriptors.1
flock /workspaces/prusa/.heavy.lock .github/fork/build.sh . anya.5 all
```

The default is the CORE One L. To build other printers, add presets after the label. `all` builds each printer that has a file in the upstream release (14 images). The table in `build.sh` gives the file name of each preset. The names are the upstream names, for example `MK4_MK4S_MK3.9_MK3.9S_anya.5.bbf`.

The output is in `build/package/` of this tree. The version stays upstream (8.0.2 since upstream
master dropped the 9.0.0 bump on 2026-11-16). The label goes after the build number:
`8.0.2+<commits>.anya.2`. The builds up to `anya.1` were 9.0.0, so the bootloader sees `anya.2` as
older: press the knob on the bootloader screen to flash it.

## Release

1. Tag the commit that was built, and push the tag: `v8.0.2-anya.<n>` for `anya/main`,
   `v8.0.2-pr.<branch>.<n>` for a topic branch. Older tags are `v9.0.0-…`.
2. `.github/fork/upload-release.sh TAG LABEL NOTES_FILE`. Notes: `RELEASE_NOTES.md` for `anya/main`,
   `notes/<branch>.md` for a topic branch. Each release is a pre-release.

GitHub Actions are off on this fork. Before you turn them on, disable the upstream workflows: they
call private Prusa workflows.
