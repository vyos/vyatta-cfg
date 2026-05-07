# AGENTS.md

## Project purpose
Legacy Vyatta configuration system: config back-end, base configuration templates, and config-mode CLI completion mechanism. Maintained for compatibility on VyOS LTS trains; new feature work goes into `vyos-1x` Python.

## Tech stack
- C/C++. Autotools build (`configure.ac`, `Makefile.am`).
- Build deps (`debian/control`): `debhelper (>= 10)`, `libglib2.0-dev`, `libboost-filesystem-dev`, `libapt-pkg-dev`, `libtool`, `flex`, `bison`, `autoconf`, `automake`, `pkg-config`, `cpio`, `dh-autoreconf`.
- Debian packaging via `dpkg-buildpackage`.

## Build / test / run
```
autoreconf -i &&./configure && make
# or, in tree:
dpkg-buildpackage -uc -us -tc -b
```
No upstream test harness — validation happens at the integration level inside the live ISO build.

## Repository layout
- `src/`, `lib/`, `scripts/`, `functions/` — C/C++ sources, helper scripts, shell functions for the legacy CLI shell (`vbash`).
- `etc/` — installed templates and skeleton config.
- `debian/` — packaging.
- `configure.ac`, `Makefile.am` — autotools.

## Cross-repo context
Pulled into ISO builds via `vyos/vyos-build` (listed in `VyOS-Networks/vyos-build-packages/repos.toml`). Pairs at runtime with `vyos/vyatta-bash` (the patched bash that hosts the CLI). Functionality has been progressively rewritten into `vyos/vyos-1x` (Python conf-mode/op-mode scripts) and `vyos/vyconf` (future OCaml session daemon).

## Conventions
- Commit/PR title: `component: T12345: description` (Phorge task ID at https://vyos.dev). Enforced by `vyos/.github/.github/workflows/check-pr-message.yml@current`.
- Release-train branches: `current` (rolling), `circinus` (1.5 LTS), `sagitta` (1.4 LTS), `equuleus` (1.3 LTS).
- Backports via `@Mergifyio backport <branch>` (Mergify built-in command).
- Reusable workflows pinned to `vyos/.github/.github/workflows/<name>.yml@current` — changes ship immediately on merge.
- Mergify config (single rule, adds `conflicts` label) lives in this repo.

## Mirror relationship
Live consumer of the gen-1 PR mirror pipeline (`pr-mirror-repo-sync.yml`). Mirror twin is `VyOS-Networks/vyatta-cfg` — only edit this canonical side; the mirror is force-pushed downstream automatically.

## Notes for future contributors
- Treat as maintenance-only. New features go to `vyos-1x`. Touch this repo only for bug fixes on LTS trains or to keep templates compatible with current Debian.
- The `pr-mirror-repo-sync.yml` workflow runs serially per merged PR; expect a downstream PR opened in `VyOS-Networks/vyatta-cfg` after merge. If `mirror-failed` label appears, see `vyos/.github` PRMirrorOnboarding.md.
- Authoritative build set is `VyOS-Networks/vyos-build-packages/repos.toml`.
