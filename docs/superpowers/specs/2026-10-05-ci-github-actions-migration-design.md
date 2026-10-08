# CI migration: Azure Pipelines → GitHub Actions (mirror libad9166-iio PR #51)

- **Date:** 2026-10-05
- **Status:** Draft for review
- **Branch:** `staging/migrate_azure_pipelines_github_actions`
- **Constraint:** All work is **local only** — commits are made locally, nothing is pushed to any remote.

## 1. Goal

Finish migrating this repo's CI from Azure Pipelines to GitHub Actions, replicating the
architecture introduced for the sibling library in
[`analogdevicesinc/libad9166-iio` PR #51](https://github.com/analogdevicesinc/libad9166-iio/pull/51),
adapted to the fact that **iio-oscilloscope is a GTK application** (produces AppImage / `.deb` /
Windows Inno-Setup installer), **not a library** (no Python bindings, no PyPI, no MSVC build). RPM
distros (Fedora/openSUSE) are built and packaged as compile-proof CI artifacts only — see S4.

The structure mirrors PR #51 (orchestrator + `setup` job + reusable per-platform workflows +
`check-artifacts` + `push-cloudsmith` + draft `push-github-release` + scheduled dispatch +
`concurrency` cancellation + SHA-pinned actions). The per-platform *internals* stay osc-native.

## 2. Decisions (confirmed with the user)

| # | Decision | Choice |
|---|----------|--------|
| 1 | Linux matrix | Ubuntu 22.04/24.04/26.04 + Debian 12/13 (apt), **plus Fedora 42/44 + openSUSE Leap 15.6/16.0 (RPM)**. See S4. |
| 2 | Windows | **Keep MinGW/MSYS2 + Inno Setup** (existing, working `adi-osc-setup.exe`). No MSVC rewrite. |
| 3 | Publishing | **Add** `check-artifacts` + `push-cloudsmith` + draft `push-github-release`. |
| 4 | Cloudsmith refs | **Keep test refs** (`adi/test-alin-repo`, `test9361-v0`/`test-v0`, `TEST_REPO_CLOUDSMITH_TOKEN`) with a labeled `# TODO(production)` swap. |

Sub-decisions (approved):

- **S1 — x86_64 Linux packaging:** produce an **AppImage** (wire in the existing `CI/appimage_x86_64`
  path); **`.deb` stays ARM-only** (today's `packaging/` wraps ARM AppImages; an x86_64 `.deb` is out
  of scope). So: x86_64 → AppImage; arm64/armhf → AppImage + `.deb`.
- **S2 — ppc64le:** include it in the QEMU `cross-arch` job alongside s390x (`fail-fast: false`).
- **S3 — `scheduled.yml` branch:** dispatch `build.yml` on `main` (this repo has no `-v0` branch).
- **S4 — Fedora/openSUSE RPM (added after initial design):** the Linux matrix adds Fedora 42/44
  and openSUSE Leap 15.6/16.0 jobs, built via `CI/build_osc_rpm.sh` (dnf/zypper deps; gtkdatabox +
  the ADI libs built from source since they are not in these distros' repos). These distros had
  **no prior support** anywhere in the project's history (only CentOS existed in the Travis era, now
  commented out; the Azure pipeline built neither). Packaging mirrors libiio: `make package` emits a
  `.rpm` (+ `.tar.gz`) as a **CI build artifact only** via `cmake/LinuxPackaging.cmake`
  (`ENABLE_PACKAGING=ON`, default OFF). The package carries osc's files only; the source-built deps
  are **not** declared or bundled, so it is a build-proof artifact, not a redistributable package.

## 3. Non-goals

- No *redistributable* RPM/DEB packages. Fedora/openSUSE jobs and `make package` produce `.rpm`/
  `.tar.gz` **CI build artifacts** (compile-proof, deps neither declared nor bundled) — not
  installable distro packages. See S4.
- No MSVC/Visual Studio Windows build.
- No Python wheel / PyPI build or publish.
- No x86_64 `.deb`.
- No change to the application source code; this is CI/build/packaging only.
- No `git push`; no changes to remote branches, releases, or Cloudsmith production repos.

## 4. Reference: PR #51 structure (the template)

`build.yml` orchestrator →
`setup` (computes `build-type`, `is-v0`, `is-release`, `cloudsmith-version`, `libiio-version`) →
reusable `_linux-builds.yml` / `_arm-builds.yml` / `_macos-builds.yml` / `_windows-builds.yml`
(called with `secrets: inherit`) → `check-artifacts` → `push-cloudsmith` → `push-github-release`.
Plus `scheduled-v0.yml` (monthly cron dispatch), top-level `concurrency` cancel-in-progress,
`permissions: contents: read`, and all third-party actions pinned to commit SHAs.

PR #51 ARM model: `arm-native` on `ubuntu-24.04-arm` (arm64 natively, arm32v7 via
`--platform linux/arm/v7`) across Ubuntu 22/26, Debian 12 (bookworm), and ADI Kuiper Debian 13
images; `cross-arch` QEMU job for ppc64le/s390x on `ubuntu-latest`; `arm-native-upstream` builds
Debian source packages.

## 5. Current state (summary)

- `build.yml` (untracked) is a thin orchestrator; only the `windows` job is active, `linux`/`arm`/
  `macOS` are commented out. No `setup`/`check`/`publish` jobs, no `concurrency`.
- `_linux_builds.yml`: Ubuntu 22/24/26 **on the runner** (not containerized); pulls libiio/ad9361/
  ad9166 from Cloudsmith; runs `CI/azure/build_osc_ubuntu.sh`; uploads raw `build/`.
- `_arm_builds.yml`: QEMU-emulates s390x/arm32v7/aarch64 via the EOL `tfcollins/...-arm-ppc` image;
  runs `CI/azure/build_osc_arm.sh`; uploads raw `build/`.
- `_macOS_builds.yml`: macos 15/26 (arm64+x64); Cloudsmith `.pkg` deps; `CI/azure/build_osc_darwin.sh`;
  uploads raw `build/`.
- `_windows_builds.yml`: MinGW/MSYS2, builds libiio/ad9361/ad9166/gtkdatabox **from source**, Inno
  Setup → `adi-osc-setup.exe`. **Actively being tested** (recent commits). No secrets.
- Legacy standalone (not wired into `build.yml`): `buildmingw.yml` (prebuilt `shooteu/...` image),
  `build_ubuntu_appimage.yml` (native `ubuntu-24.04-arm`, Debian trixie, AppImage + `.deb` via
  `CI/setup_and_build.sh`).
- `azure-pipelines.yml` present (original, 2 jobs: Linux x86_64 + ARM; macOS commented out).
- Build scripts under `CI/azure/`, `CI/docker/` (MinGW), `CI/appimage_{x86_64,aarch64,armhf}/`,
  `CI/setup_and_build.sh`, `packaging/` (deb that wraps the AppImage, ARM-only).
- Deps: `glib-2.0 gtk+-3.0 gthread-2.0 gtkdatabox>=1.0.0 fftw3 libxml-2.0 libcurl jansson matio
  libiio` (REQUIRED), libserialport optional. libad9361-iio/libad9166-iio are **runtime plugins**
  (bundled, not linked).

## 6. Target architecture

### 6.1 `.github/workflows/` layout

```
build.yml            orchestrator
_linux_builds.yml    containerized deb-distro matrix
_arm_builds.yml      arm-native (ubuntu-24.04-arm) + cross-arch QEMU
_macOS_builds.yml    macos 15/26 arm64+x64
_windows_builds.yml  MinGW/MSYS2 + Inno Setup (kept)
scheduled.yml        monthly cron dispatch
```

### 6.2 `build.yml` (orchestrator)

- **Triggers (unchanged):** `workflow_dispatch`; `push` to `main`/`master`/`staging/*`/`20*` and
  tags `v*`; `pull_request` to `main`/`master`/`20*`.
- **Top-level** `permissions: contents: read` and `concurrency: { group: ${{ github.workflow }}-${{ github.ref }}, cancel-in-progress: true }` (as a top-level key — PR #51 fixed an indentation bug where it was nested under `on:`).
- **`setup` job** (`runs-on: ubuntu-latest`) outputs:
  - `build-type` = `Release` on `refs/tags/v*`, else `RelWithDebInfo`.
  - `is-release` = `true` on `v*` tags.
  - `is-v0` = `true` on the designated nightly branch (`main` per S3).
  - `cloudsmith-version` = publish version string (tag-derived on release; `iio-oscilloscope-v0~latest` on v0).
- **Platform jobs** `linux` / `arm` / `macos` / `windows`: `needs: setup`,
  `uses: ./.github/workflows/_*_builds.yml`, `secrets: inherit`. `linux`/`arm`/`macos` are called
  `with: { build-type: ${{ needs.setup.outputs.build-type }} }`. **`windows` is not** — it keeps its
  existing `libiio-branch` / `libad9361-branch` / `libad9166-branch` inputs and builds
  `RelWithDebInfo` fixed in `build_mingw.sh` (mirrors PR #51 dropping `build-type` from Windows).
- **`check-artifacts`** (`needs: [setup, linux, arm, macos, windows]`, gated `is-v0 || is-release`,
  `if: always() && !cancelled() && !contains(needs.*.result,'failure')`): download all artifacts,
  fail on any empty artifact dir, write + upload `artifact_manifest.txt`.
- **`push-cloudsmith`** (`needs: [setup, check-artifacts]`, gated `is-v0 || is-release`): install
  `cloudsmith-cli`, push each artifact to the **test** Cloudsmith repo (per decision 4), stripping
  the `.g<hash>` git suffix for stable names; zip Windows artifacts before upload. `# TODO(production)`
  marks the `adi/external` + `CLOUDSMITH_API_KEY` swap.
- **`push-github-release`** (`needs: [setup, check-artifacts]`, gated `is-release`,
  `permissions: contents: write`): download all artifacts, create a **draft** release
  (`softprops/action-gh-release`, SHA-pinned) with `generate_release_notes: true`.
- **Dropped from PR #51:** `build-pypi`, `push-pypi`.

### 6.3 `_linux_builds.yml` (containerized, deb distros)

- `on: workflow_call` with input `build-type` (string, required).
- Matrix (`container.image` / `artifact-name`), `fail-fast: false`:
  - `ubuntu:22.04` → `Linux-Ubuntu-22.04`
  - `ubuntu:24.04` → `Linux-Ubuntu-24.04`
  - `ubuntu:26.04` → `Linux-Ubuntu-26.04`
  - `debian:bookworm` → `Linux-Debian-12`
  - `debian:trixie` → `Linux-Debian-13`
- Runs inside the matrix container (`container: { image: ... }`) on `ubuntu-latest`.
- Steps: install build deps (apt) → checkout → fetch libiio/ad9361/ad9166 from Cloudsmith (test
  refs, `package_name` = `<artifact-name>.deb`) → install those `.deb`s → `install_gtkdatabox` →
  build osc with `CMAKE_BUILD_TYPE=${{ inputs.build-type }}` → **build x86_64 AppImage** (S1) →
  upload AppImage (+ raw build on failure for debugging).
- Dependency-package names must match the new artifact-name strings (e.g. `Linux-Debian-12.deb`),
  which assumes the corresponding Cloudsmith artifacts exist; see Risks §9.

### 6.4 `_arm_builds.yml` (rework — native + QEMU)

Replaces the all-QEMU design and **absorbs** `build_ubuntu_appimage.yml`.

- `on: workflow_call` with input `build-type`.
- **Job `arm-native`** — `runs-on: ubuntu-24.04-arm`, `fail-fast: false`. Matrix
  (`image` / `platform-flag` / `artifact-name`):
  - stock `arm64v8/debian:trixie` → `osc-linux-arm64` (AppImage + `.deb`)
  - stock `arm32v7/debian:trixie` + `--platform linux/arm/v7` → `osc-linux-armhf` (AppImage + `.deb`)
  - (Optionally Ubuntu 22/26 arm64/arm32v7 build-smoke entries to match PR #51 breadth — see Open
    items O3.)
  - Build path reuses `CI/setup_and_build.sh <arch>` → `CI/appimage_<arch>/create_appimage.sh` +
    `packaging/build-iio-osc-deb.sh`. Deps via the public ADI Kuiper Cloudsmith **apt** repo
    (`setup.deb.sh` → `libad9361-dev`, `libad9166-dev`) layered on the stock Debian image, as
    today — no token.
- **Job `cross-arch`** — `runs-on: ubuntu-latest`, QEMU (`docker/setup-qemu-action`, SHA-pinned),
  `fail-fast: false`. Matrix: `s390x`, `ppc64le` (S2) on a vanilla `ubuntu:22.04` container.
  Pulls libiio/ad9361/ad9166 from Cloudsmith (test refs), builds osc (smoke; raw `build/`),
  uploads. Replaces the EOL `tfcollins` image.
- Artifact names chosen so `push-cloudsmith` and `check-artifacts` see non-empty dirs.

### 6.5 `_macOS_builds.yml` (kept, minor)

- Add `build-type` input, thread `CMAKE_BUILD_TYPE` into `CI/azure/build_osc_darwin.sh` (→ relocated
  path, §7). Matrix unchanged (macos 15/26, arm64+x64). Still uploads raw `build/` (no `.pkg`/`.dmg`
  produced for osc — unchanged; non-goal to add one).

### 6.6 `_windows_builds.yml` (kept)

- Unchanged build internals (MinGW/MSYS2, from-source deps, Inno Setup → `adi-osc-setup.exe`).
- Wiring: callable from the orchestrator; its artifact (`Windows-2022-x64` →
  `artifact/adi-osc-setup.exe`) flows into `check-artifacts` / `push-cloudsmith` (zipped) /
  `push-github-release`. Keeps its existing `libiio-branch`/`libad9361-branch`/`libad9166-branch`
  inputs (defaults preserved).

### 6.7 `scheduled.yml`

- `on: schedule: [{ cron: '7 3 1 * *' }]` (monthly; off-:00 minute), `permissions: actions: write`,
  job runs `gh workflow run build.yml --ref main` with `GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}`.

## 7. Script changes

- **Relocate** `CI/azure/build_osc_ubuntu.sh`, `build_osc_arm.sh`, `build_osc_darwin.sh` → `CI/`
  (de-Azure-ify, matching PR #51's `prepare_assets.sh` move); update all workflow references. Drop
  the unused `CI/azure/ci-ubuntu.sh`. Remove the now-empty `CI/azure/` directory.
- **x86_64 AppImage wiring (S1):** `_linux_builds.yml` invokes the existing
  `CI/appimage_x86_64/{install_deps.sh,build_osc.sh,create_appimage.sh}` to emit
  `ADI_IIO_Oscilloscope-x86_64.AppImage`. (These scripts build libiio/ad9361/ad9166 from source;
  acceptable for the AppImage path. No new packaging code.)
- `build_osc_darwin.sh` / `build_osc_ubuntu.sh` / `build_osc_arm.sh`: honor
  `CMAKE_BUILD_TYPE` from the environment (passed from the workflow `build-type`).

## 8. Removals

- `azure-pipelines.yml`.
- `.github/workflows/buildmingw.yml` (legacy; superseded by `_windows_builds.yml`).
- `.github/workflows/build_ubuntu_appimage.yml` (folded into `_arm_builds.yml`).
- `CI/azure/` (after relocating the three build scripts).
- `.github/workflows/main.yml` is already deleted in the working tree (keep deleted).

## 9. Risks & assumptions

- **Cloudsmith artifact names:** fetches assume dependency artifacts exist under the new
  `artifact-name` strings (`Linux-Debian-12.deb`, `Ubuntu-22.04-ppc64le.deb`, etc.). If the test
  repo lacks them, those matrix legs fail at the fetch step. Mitigation: `fail-fast: false`; names
  documented for the production swap.
- **ppc64le (S2):** may not build cleanly under QEMU; isolated by `fail-fast: false`.
- **Containerized Linux:** moving from runner to container changes the dependency-install surface;
  the apt list from `build_osc_ubuntu.sh` must cover a bare container (add `build-essential git
  cmake sudo wget curl` etc.).
- **No local GH Actions execution:** correctness is verified statically (see §10), not by running
  the pipeline, since we are local-only and will not push.
- **Secrets** (`TEST_REPO_CLOUDSMITH_TOKEN`) are not available locally; publish jobs cannot be
  exercised locally — validated by lint + review only.

## 10. Verification (local-only)

- `actionlint` on every workflow file (syntax, matrix, expression, SHA-pin checks).
- `shellcheck` on changed/relocated shell scripts.
- YAML parse check on all workflows.
- Manual trace of job graph (`needs`, gating `if:`, artifact name ↔ consumer matching).
- `git` sanity: relocated-script references updated, no dangling paths; removed files gone.

## 11. Open items for the plan

- **O1:** Confirm the exact `cloudsmith-version` / publish naming for osc artifacts in the test repo.
- **O2:** S3 sets the `scheduled.yml` dispatch branch to `main`; confirm that is the intended
  nightly branch before enabling the schedule (repoint if a dedicated release branch is created).
- **O3:** Decide whether `arm-native` adds Ubuntu 22/26 arm smoke legs (PR #51 breadth) or stays at
  Debian 12/13 arm64+armhf (the app's real AppImage/deb targets). Default: the latter.

## 12. Implementation phasing (detail deferred to the plan)

1. Orchestrator skeleton: `setup` job, `concurrency`, enable all four platform calls, pass `build-type`.
2. `_linux_builds.yml` containerization + deb matrix + x86_64 AppImage.
3. `_arm_builds.yml` native+QEMU rework; fold in `build_ubuntu_appimage.yml`.
4. `_macOS_builds.yml` + `_windows_builds.yml` wiring (`build-type`, orchestrator inputs).
5. `check-artifacts` + `push-cloudsmith` + `push-github-release`.
6. `scheduled.yml`.
7. Script relocation + de-Azure-ify + removals.
8. Hardening: SHA-pin actions, trailing newlines, final `actionlint`/`shellcheck` pass.
