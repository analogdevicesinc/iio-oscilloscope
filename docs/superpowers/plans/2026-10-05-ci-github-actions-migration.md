# CI GitHub Actions Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish migrating this repo's CI from Azure Pipelines to GitHub Actions, mirroring the structure of `analogdevicesinc/libad9166-iio` PR #51, adapted for this GTK application.

**Architecture:** A `build.yml` orchestrator runs a `setup` job, then calls reusable per-platform workflows (`_linux_builds.yml`, `_arm_builds.yml`, `_macOS_builds.yml`, `_windows_builds.yml`), then `check-artifacts` → `push-cloudsmith` → draft `push-github-release`. ARM moves to native `ubuntu-24.04-arm` runners (arm64/arm32v7) plus a QEMU `cross-arch` job (s390x/ppc64le). Legacy standalone workflows and Azure config are removed.

**Tech Stack:** GitHub Actions (reusable workflows), Docker containers (Ubuntu/Debian), QEMU, MSYS2/MinGW, Cloudsmith CLI + `cloudsmith_helper.py`, Inno Setup, AppImage (linuxdeploy / appimagetool / makeself), debhelper.

**Spec:** `docs/superpowers/specs/2026-10-05-ci-github-actions-migration-design.md`

## Global Constraints

- **Local only.** Make local commits; never `git push`; never create remote releases or push to Cloudsmith production. (Publish jobs are authored but only run in real CI.)
- **Keep test Cloudsmith refs** verbatim: repo `adi/test-alin-repo` (ad9361=`test9361-v0`, ad9166=`test-v0`), libiio `external`/`libiio-v0~latest`, secret `TEST_REPO_CLOUDSMITH_TOKEN` → env `CLOUDSMITH_API_KEY`. Mark the production swap with a `# TODO(production):` comment, never change the values.
- **Windows stays MinGW/MSYS2 + Inno Setup**; it builds `RelWithDebInfo` from source and is NOT passed `build-type`.
- **Deb distros only**: Ubuntu 22.04/24.04/26.04 + Debian 12/13. No RPM, Fedora, openSUSE, MSVC, PyPI, or x86_64 `.deb`.
- **No application source changes** — CI/build/packaging files only.
- **Pin every third-party action to a commit SHA** with a trailing `# vX.Y.Z` comment.
- **Per-task gate:** `"$HOME/.tools/actionlint/actionlint.exe" .github/workflows/<file>.yml` must exit 0 for every workflow touched. Builds cannot run locally (no docker/python); beyond lint, verify by tracing the job graph and artifact-name wiring.
- **Commit style:** `ci: <subject>` (or `docs:`), imperative, no `Signed-off-by`, no Claude attribution (per repo CLAUDE.md).

## Pinned action SHAs (reuse verbatim; from PR #51)

```
actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1          # v7.0.1
actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a   # v7.0.1
actions/download-artifact@3e5f45b2cfb9172054b4087a40e8e0b5a5461e7c # v8.0.1
actions/setup-python@5fda3b95a4ea91299a34e894583c3862153e4b97      # v7.0.0
astral-sh/setup-uv@c18668ad3cf93ea998bef934396af7bb5c839dc7        # v10.2.0
docker/setup-qemu-action@99012661954931238ded8c8b007157a8430204e1  # v4.4.0
softprops/action-gh-release@3bb12739c298aeb8a4eeaf626c5b8d85266b0e65 # v2.6.2
```
For actions not in PR #51 (`msys2/setup-msys2`), resolve the SHA for the current major at implementation time:
`gh api repos/msys2/setup-msys2/commits/v2 -q .sha` and pin with a `# v2` comment.

## Review Focus

- **Empty-artifact gating:** `check-artifacts` must fail if any downloaded artifact dir is empty, and must only run on `is-v0 || is-release` with `always() && !cancelled() && !contains(needs.*.result,'failure')`. Pinned in Task 7.
- **Artifact-name ↔ Cloudsmith package match:** each dependency fetch uses `package_name` = `<artifact-name>.<ext>`; a renamed matrix leg silently breaks the fetch. Verified by trace in Tasks 2/3.
- **Bare container missing basics:** containerized Linux/ARM/cross-arch jobs run as root in minimal images lacking `sudo`, `git`, `build-essential`, `wget`, `curl`; these must be installed before build scripts that assume them. Pinned in Tasks 2/3.
- **Reusable-workflow input mismatch:** `build.yml` must only pass `with:` keys declared in each called workflow's `on.workflow_call.inputs` (actionlint enforces). Ordering: inputs added in Tasks 2–4 before wiring in Task 6.
- **QEMU arch build failure isolation:** `cross-arch` (s390x/ppc64le) must set `fail-fast: false` so one flaky arch cannot sink the matrix. Pinned in Task 3.

---

### Task 1: De-Azure — relocate build scripts, delete `azure-pipelines.yml`

**Files:**
- Move: `CI/azure/build_osc_ubuntu.sh` → `CI/build_osc_ubuntu.sh`
- Move: `CI/azure/build_osc_arm.sh` → `CI/build_osc_arm.sh`
- Move: `CI/azure/build_osc_darwin.sh` → `CI/build_osc_darwin.sh`
- Delete: `CI/azure/ci-ubuntu.sh` (unused), then the empty `CI/azure/` dir
- Delete: `azure-pipelines.yml`
- Modify (reference path only): `.github/workflows/_linux_builds.yml`, `_arm_builds.yml`, `_macOS_builds.yml`

**Interfaces:**
- Produces: build scripts at `CI/build_osc_{ubuntu,arm,darwin}.sh` (same `$@`-dispatch functions: `install_apt_pkgs`, `install_gtkdatabox`, `install_adi_debs`/`install_adi_pkgs`, `install_deps`, `build_osc`).

- [ ] **Step 1: Move scripts with git**

```bash
cd /c/Git/iio-oscilloscope
git mv CI/azure/build_osc_ubuntu.sh  CI/build_osc_ubuntu.sh
git mv CI/azure/build_osc_arm.sh     CI/build_osc_arm.sh
git mv CI/azure/build_osc_darwin.sh  CI/build_osc_darwin.sh
git rm CI/azure/ci-ubuntu.sh
git rm azure-pipelines.yml
```

- [ ] **Step 2: Update references in the three current workflows**

Replace every `./CI/azure/build_osc_ubuntu.sh` → `./CI/build_osc_ubuntu.sh` in `_linux_builds.yml`; `./CI/azure/build_osc_arm.sh` → `./CI/build_osc_arm.sh` in `_arm_builds.yml`; `./CI/azure/build_osc_darwin.sh` → `./CI/build_osc_darwin.sh` in `_macOS_builds.yml`.

- [ ] **Step 3: Confirm no dangling references remain**

Run: `grep -rn "CI/azure" . --include=*.yml --include=*.sh` (ignore anything under `docs/`)
Expected: no matches outside the plan/spec docs.

- [ ] **Step 4: Lint the touched workflows**

Run: `"$HOME/.tools/actionlint/actionlint.exe" .github/workflows/_linux_builds.yml .github/workflows/_arm_builds.yml .github/workflows/_macOS_builds.yml`
Expected: no new errors introduced by this task (the pre-existing `ubuntu-26.04` runner-label warning in `_linux_builds.yml` is addressed in Task 2).

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "ci: relocate build scripts out of CI/azure and drop azure-pipelines.yml"
```

---

### Task 2: Rework `_linux_builds.yml` — containerized deb matrix + build-type + x86_64 AppImage

**Files:**
- Modify: `.github/workflows/_linux_builds.yml`

**Interfaces:**
- Consumes: `build_osc_ubuntu.sh` (Task 1); `CI/appimage_x86_64/{install_deps.sh,build_osc.sh,create_appimage.sh}`.
- Produces: `on.workflow_call.inputs.build-type` (string, required); artifacts named `Linux-Ubuntu-22.04`/`-24.04`/`-26.04`, `Linux-Debian-12`, `Linux-Debian-13`.

- [ ] **Step 1: Rewrite the workflow**

Target shape (runner `ubuntu-latest`, job runs inside `container.image`):

```yaml
name: Linux Builds
on:
  workflow_dispatch:
  workflow_call:
    inputs:
      build-type:
        required: true
        type: string
jobs:
  linux:
    name: ${{ matrix.artifact-name }}
    runs-on: ubuntu-latest
    timeout-minutes: 30
    strategy:
      fail-fast: false
      matrix:
        include:
          - image: ubuntu:22.04
            artifact-name: Linux-Ubuntu-22.04
          - image: ubuntu:24.04
            artifact-name: Linux-Ubuntu-24.04
          - image: ubuntu:26.04
            artifact-name: Linux-Ubuntu-26.04
          - image: debian:bookworm
            artifact-name: Linux-Debian-12
          - image: debian:trixie
            artifact-name: Linux-Debian-13
    container:
      image: ${{ matrix.image }}
    env:
      CMAKE_BUILD_TYPE: ${{ inputs.build-type }}
    steps:
      - name: Install base packages
        run: |
          set -e
          export DEBIAN_FRONTEND=noninteractive
          apt-get update
          apt-get install -y sudo build-essential git cmake wget curl ca-certificates \
            python3 python3-pip autoconf automake libtool
      - name: Checkout
        uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
        with:
          fetch-depth: 1
      - name: Setup Python
        uses: actions/setup-python@5fda3b95a4ea91299a34e894583c3862153e4b97 # v7.0.0
        with:
          python-version: '3.11'
      - run: pip install requests
      - name: Get ADI dependencies from Cloudsmith
        env:
          CLOUDSMITH_API_KEY: ${{ secrets.TEST_REPO_CLOUDSMITH_TOKEN }} # TODO(production): CLOUDSMITH_API_KEY
        run: |
          set -e
          wget -q https://raw.githubusercontent.com/analogdevicesinc/wiki-scripts/refs/heads/main/utils/cloudsmith_utils/cloudsmith_helper.py \
            -O /tmp/cloudsmith_helper.py
          mkdir -p download && cd download
          # libiio from external; libad9361/libad9166 from the test repo. # TODO(production): adi/external + *-v0~latest
          python3 /tmp/cloudsmith_helper.py --method get_artifacts_from_location \
            --repo external --package_version "libiio-v0~latest" --package_name "${{ matrix.artifact-name }}.deb"
          python3 /tmp/cloudsmith_helper.py --method get_artifacts_from_location \
            --repo adi/test-alin-repo --package_version "test9361-v0" --package_name "${{ matrix.artifact-name }}.deb"
          python3 /tmp/cloudsmith_helper.py --method get_artifacts_from_location \
            --repo adi/test-alin-repo --package_version "test-v0" --package_name "${{ matrix.artifact-name }}.deb"
      - name: Install dependencies and build
        run: |
          set -e
          ./CI/build_osc_ubuntu.sh install_apt_pkgs
          ./CI/build_osc_ubuntu.sh install_adi_debs
          ./CI/build_osc_ubuntu.sh install_gtkdatabox
          ./CI/build_osc_ubuntu.sh build_osc
      - name: Upload build artifacts
        if: github.event_name != 'pull_request'
        uses: actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a # v7.0.1
        with:
          name: ${{ matrix.artifact-name }}
          path: build/
          if-no-files-found: error
```

- [ ] **Step 2: Add the x86_64 AppImage job (S1)**

Append a second job `appimage-x86_64` (runs-on `ubuntu-22.04`, no container — needs FUSE/host) that runs the existing `CI/appimage_x86_64/install_deps.sh` (all `install_*` steps), `build_osc.sh`, `create_appimage.sh`, then uploads `ADI_IIO_Oscilloscope-x86_64.AppImage` as artifact `osc-linux-x86_64`. Pin `actions/checkout` and `actions/upload-artifact` to the SHAs above; `if: github.event_name != 'pull_request'` on upload.

- [ ] **Step 3: Lint**

Run: `"$HOME/.tools/actionlint/actionlint.exe" .github/workflows/_linux_builds.yml`
Expected: exit 0, no errors. (Container image strings are not runner labels, so the old `ubuntu-26.04` label warning is gone.)

- [ ] **Step 4: Trace**

Confirm: every matrix `artifact-name` is used as the Cloudsmith `package_name` stem; `CMAKE_BUILD_TYPE` env is set from `inputs.build-type`; base-package step installs `sudo` before scripts that call `sudo`.

- [ ] **Step 5: Commit**

```bash
git add .github/workflows/_linux_builds.yml
git commit -m "ci: containerize Linux builds, add Debian 12/13 and x86_64 AppImage"
```

---

### Task 3: Rework `_arm_builds.yml` — native ARM + QEMU cross-arch; fold in AppImage/deb

**Files:**
- Modify: `.github/workflows/_arm_builds.yml`

**Interfaces:**
- Consumes: `CI/setup_and_build.sh` (arm-native, produces AppImage + `.deb`); `CI/build_osc_arm.sh` (cross-arch smoke build).
- Produces: `on.workflow_call.inputs.build-type`; artifacts `osc-linux-arm64`, `osc-linux-armhf` (AppImage + deb), `Ubuntu-22.04-s390x`, `Ubuntu-22.04-ppc64le` (raw build).

- [ ] **Step 1: Write the `arm-native` job**

```yaml
name: ARM Builds
on:
  workflow_dispatch:
  workflow_call:
    inputs:
      build-type:
        required: true
        type: string
env:
  APP_NAME: iio-oscilloscope
jobs:
  arm-native:
    name: ${{ matrix.architecture }}
    runs-on: ubuntu-24.04-arm
    timeout-minutes: 45
    strategy:
      fail-fast: false
      matrix:
        include:
          - docker_image: arm64v8/debian:trixie
            architecture: aarch64
            platform_flag: ''
          - docker_image: arm32v7/debian:trixie
            architecture: armhf
            platform_flag: '--platform linux/arm/v7'
    steps:
      - uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
        with:
          path: ${{ env.APP_NAME }}
      - name: Start persistent container
        run: |
          docker run -d ${{ matrix.platform_flag }} \
            --name osc-${{ matrix.architecture }} \
            -v "$GITHUB_WORKSPACE/${{ env.APP_NAME }}:/workspace/${{ env.APP_NAME }}" \
            -w /workspace/${{ env.APP_NAME }} \
            ${{ matrix.docker_image }} tail -f /dev/null
      - name: Build AppImage + deb
        run: docker exec osc-${{ matrix.architecture }} sh -c "./CI/setup_and_build.sh ${{ matrix.architecture }}" 2>&1
      - name: Copy artifacts
        run: |
          docker cp osc-${{ matrix.architecture }}:/workspace/iio-oscilloscope-${{ matrix.architecture }}.AppImage ${{ github.workspace }}/
          docker exec osc-${{ matrix.architecture }} sh -c "mkdir -p /tmp/deb && cp /workspace/iio-oscilloscope_* /tmp/deb/"
          docker cp osc-${{ matrix.architecture }}:/tmp/deb/. ${{ github.workspace }}/
      - uses: actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a # v7.0.1
        if: github.event_name != 'pull_request'
        with:
          name: osc-linux-${{ matrix.architecture }}
          path: |
            ${{ github.workspace }}/iio-oscilloscope-${{ matrix.architecture }}.AppImage
            ${{ github.workspace }}/iio-oscilloscope_*
          if-no-files-found: error
```

(This is the folded-in `build_ubuntu_appimage.yml` logic, now a job of `_arm_builds.yml`. `setup_and_build.sh` already pulls `libad9361-dev`/`libad9166-dev` from the public ADI Kuiper apt repo — no token.)

- [ ] **Step 2: Write the `cross-arch` QEMU job (S2)**

```yaml
  cross-arch:
    name: ${{ matrix.artifact-name }}
    runs-on: ubuntu-latest
    timeout-minutes: 60
    strategy:
      fail-fast: false
      matrix:
        include:
          - arch: s390x
            artifact-name: Ubuntu-22.04-s390x
          - arch: ppc64le
            artifact-name: Ubuntu-22.04-ppc64le
    env:
      CMAKE_BUILD_TYPE: ${{ inputs.build-type }}
    steps:
      - uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
        with:
          fetch-depth: 1
      - uses: actions/setup-python@5fda3b95a4ea91299a34e894583c3862153e4b97 # v7.0.0
        with:
          python-version: '3.11'
      - run: pip install requests
      - name: Get ADI dependencies from Cloudsmith
        env:
          CLOUDSMITH_API_KEY: ${{ secrets.TEST_REPO_CLOUDSMITH_TOKEN }} # TODO(production): CLOUDSMITH_API_KEY
        run: |
          set -e
          wget -q https://raw.githubusercontent.com/analogdevicesinc/wiki-scripts/refs/heads/main/utils/cloudsmith_utils/cloudsmith_helper.py \
            -O /tmp/cloudsmith_helper.py
          mkdir -p download && cd download
          for dep_repo in "external:libiio-v0~latest" "external:libad9361-iio-v0~latest" "external:libad9166-iio-v0~latest"; do
            repo="${dep_repo%%:*}"; ver="${dep_repo##*:}"
            python3 /tmp/cloudsmith_helper.py --method get_artifacts_from_location \
              --repo "$repo" --package_version "$ver" --package_name "${{ matrix.artifact-name }}.deb"
          done
      - uses: docker/setup-qemu-action@99012661954931238ded8c8b007157a8430204e1 # v4.4.0
      - name: Build (QEMU)
        run: |
          set -e
          docker run --rm -m=4g --platform "linux/${{ matrix.arch }}" \
            -e ARTIFACTNAME=${{ matrix.artifact-name }} \
            -e CMAKE_BUILD_TYPE=${{ inputs.build-type }} \
            -v "${{ github.workspace }}":/ci ubuntu:22.04 \
            /bin/bash -c "
              set -e
              export DEBIAN_FRONTEND=noninteractive
              apt-get update
              apt-get install -y sudo build-essential git cmake wget curl ca-certificates autoconf automake libtool
              cd /ci
              ./CI/build_osc_arm.sh install_apt_pkgs
              ./CI/build_osc_arm.sh install_adi_debs
              ./CI/build_osc_arm.sh install_gtkdatabox
              ./CI/build_osc_arm.sh build_osc
            "
      - name: Fix permissions
        run: sudo chown -R "$(id -u):$(id -g)" build
      - uses: actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a # v7.0.1
        if: github.event_name != 'pull_request'
        with:
          name: ${{ matrix.artifact-name }}
          path: build/
          if-no-files-found: error
```

Note: `build_osc_arm.sh install_adi_debs` consumes `./download/*.deb`; the QEMU step mounts the repo at `/ci`, and `download/` is created in the host step above, so it is present at `/ci/download`. Confirm the script's `dpkg -i ./download/*.deb` path resolves from `/ci` (cwd) — it does.

- [ ] **Step 3: Lint**

Run: `"$HOME/.tools/actionlint/actionlint.exe" .github/workflows/_arm_builds.yml`
Expected: exit 0. `ubuntu-24.04-arm` and `ubuntu-latest` are known labels.

- [ ] **Step 4: Trace**

Confirm `fail-fast: false` on both jobs; `cross-arch` artifact-name used as Cloudsmith `package_name`; base packages (incl. `sudo`) installed in the QEMU container before scripts.

- [ ] **Step 5: Commit**

```bash
git add .github/workflows/_arm_builds.yml
git commit -m "ci: rework ARM to native runners plus QEMU cross-arch (s390x/ppc64le)"
```

---

### Task 4: `_macOS_builds.yml` — add `build-type` input

**Files:**
- Modify: `.github/workflows/_macOS_builds.yml`

**Interfaces:**
- Produces: `on.workflow_call.inputs.build-type` (string, required); matrix/artifacts unchanged (`macOS-15-arm64`, `macOS-15-x64`, `macOS-26-arm64`, `macOS-26-x64`).

- [ ] **Step 1: Add the input and thread the env**

Add under `on.workflow_call`:
```yaml
    inputs:
      build-type:
        required: true
        type: string
```
Add to the job `env:` block: `CMAKE_BUILD_TYPE: ${{ inputs.build-type }}` (alongside the existing `ARTIFACTNAME`). No other changes; `build_osc_darwin.sh` already honors `CMAKE_BUILD_TYPE` if set (verify; if it hardcodes, add a `${CMAKE_BUILD_TYPE:-RelWithDebInfo}` to its cmake invocation in a follow-up step within this task).

- [ ] **Step 2: Verify the darwin script honors the env**

Run: `grep -n "CMAKE_BUILD_TYPE\|cmake" CI/build_osc_darwin.sh`
If `cmake` is invoked without `-DCMAKE_BUILD_TYPE`, add `-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE:-RelWithDebInfo}` to that invocation. Commit the script change as part of this task.

- [ ] **Step 3: Lint**

Run: `"$HOME/.tools/actionlint/actionlint.exe" .github/workflows/_macOS_builds.yml`
Expected: exit 0.

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/_macOS_builds.yml CI/build_osc_darwin.sh
git commit -m "ci: thread build-type into macOS builds"
```

---

### Task 5: `_windows_builds.yml` — confirm callable + SHA-pin msys2

**Files:**
- Modify: `.github/workflows/_windows_builds.yml`

**Interfaces:**
- Consumes: nothing new.
- Produces: unchanged — inputs `libiio-branch`/`libad9361-branch`/`libad9166-branch`; artifact `Windows-2022-x64` → `artifact/adi-osc-setup.exe`.

- [ ] **Step 1: Pin `msys2/setup-msys2`**

Resolve SHA: `gh api repos/msys2/setup-msys2/commits/v2 -q .sha`
Replace `uses: msys2/setup-msys2@v2` with `msys2/setup-msys2@<sha> # v2`. Pin `actions/checkout` and `actions/upload-artifact` here to the standard SHAs too.

- [ ] **Step 2: Confirm no `build-type` input is required**

Confirm the orchestrator (Task 6) will call this workflow WITHOUT `build-type`. No input change here.

- [ ] **Step 3: Lint**

Run: `"$HOME/.tools/actionlint/actionlint.exe" .github/workflows/_windows_builds.yml`
Expected: exit 0.

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/_windows_builds.yml
git commit -m "ci: pin Windows workflow actions to SHAs"
```

---

### Task 6: `build.yml` orchestrator — `setup` job + wire all platforms + concurrency

**Files:**
- Modify: `.github/workflows/build.yml`

**Interfaces:**
- Consumes: the four reusable workflows (now all accept their inputs).
- Produces: `setup` outputs `build-type`, `is-v0`, `is-release`, `cloudsmith-version` consumed by Tasks 6/7 jobs.

- [ ] **Step 1: Write triggers, permissions, concurrency, setup job**

```yaml
name: Build and Deploy iio-oscilloscope
permissions:
  contents: read
on:
  workflow_dispatch:
  push:
    branches: [main, master, 'staging/*', '20*']
    tags: ['v*']
  pull_request:
    branches: [main, master, '20*']
concurrency:
  group: ${{ github.workflow }}-${{ github.ref }}
  cancel-in-progress: true
jobs:
  setup:
    runs-on: ubuntu-latest
    outputs:
      build-type: ${{ steps.set.outputs.build-type }}
      is-v0: ${{ steps.set.outputs.is-v0 }}
      is-release: ${{ steps.set.outputs.is-release }}
      cloudsmith-version: ${{ steps.set.outputs.cloudsmith-version }}
    steps:
      - id: set
        shell: bash
        run: |
          if [[ "${GITHUB_REF}" == refs/tags/v* ]]; then
            echo "build-type=Release" >> "${GITHUB_OUTPUT}"
            echo "is-release=true" >> "${GITHUB_OUTPUT}"
            echo "cloudsmith-version=iio-oscilloscope-${GITHUB_REF#refs/tags/}" >> "${GITHUB_OUTPUT}"
          else
            echo "build-type=RelWithDebInfo" >> "${GITHUB_OUTPUT}"
            echo "is-release=false" >> "${GITHUB_OUTPUT}"
          fi
          if [[ "${GITHUB_REF}" == "refs/heads/main" ]]; then   # O2: nightly branch
            echo "is-v0=true" >> "${GITHUB_OUTPUT}"
            echo "cloudsmith-version=iio-oscilloscope-v0~latest" >> "${GITHUB_OUTPUT}"
          else
            echo "is-v0=false" >> "${GITHUB_OUTPUT}"
          fi
```

- [ ] **Step 2: Wire the platform jobs**

```yaml
  linux:
    needs: setup
    uses: ./.github/workflows/_linux_builds.yml
    with:
      build-type: ${{ needs.setup.outputs.build-type }}
    secrets: inherit
  arm:
    needs: setup
    uses: ./.github/workflows/_arm_builds.yml
    with:
      build-type: ${{ needs.setup.outputs.build-type }}
    secrets: inherit
  macos:
    needs: setup
    uses: ./.github/workflows/_macOS_builds.yml
    with:
      build-type: ${{ needs.setup.outputs.build-type }}
    secrets: inherit
  windows:
    needs: setup
    uses: ./.github/workflows/_windows_builds.yml
    secrets: inherit
```

- [ ] **Step 3: Lint the whole set**

Run: `"$HOME/.tools/actionlint/actionlint.exe" .github/workflows/build.yml`
Expected: exit 0. actionlint validates that each `with: build-type` matches a declared input and that `windows` (no inputs passed) is legal.

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/build.yml
git commit -m "ci: add setup job and wire all platforms with concurrency"
```

---

### Task 7: Publishing jobs — `check-artifacts`, `push-cloudsmith`, `push-github-release`

**Files:**
- Modify: `.github/workflows/build.yml`

**Interfaces:**
- Consumes: all platform artifacts + `setup` outputs.

- [ ] **Step 1: `check-artifacts`**

```yaml
  check-artifacts:
    if: |
      always() && !cancelled() &&
      !contains(needs.*.result, 'failure') &&
      (needs.setup.outputs.is-v0 == 'true' || needs.setup.outputs.is-release == 'true')
    needs: [setup, linux, arm, macos, windows]
    runs-on: ubuntu-latest
    timeout-minutes: 10
    steps:
      - uses: actions/download-artifact@3e5f45b2cfb9172054b4087a40e8e0b5a5461e7c # v8.0.1
        with:
          path: artifacts/
      - name: Check artifacts non-empty
        run: |
          set -e
          MISSING=0
          for dir in artifacts/*/; do
            name=$(basename "$dir"); count=$(find "$dir" -type f | wc -l)
            if [ "$count" -eq 0 ]; then echo "EMPTY: $name"; MISSING=$((MISSING+1)); else echo "OK: $name ($count)"; fi
          done
          find artifacts -type f | sort | tee artifact_manifest.txt
          [ "$MISSING" -eq 0 ] || { echo "ERROR: $MISSING empty"; exit 1; }
      - uses: actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a # v7.0.1
        with:
          name: Artifact-manifest
          path: artifact_manifest.txt
```

- [ ] **Step 2: `push-cloudsmith`** (test repo; `# TODO(production)`)

```yaml
  push-cloudsmith:
    if: needs.setup.outputs.is-v0 == 'true' || needs.setup.outputs.is-release == 'true'
    needs: [setup, check-artifacts]
    runs-on: ubuntu-latest
    timeout-minutes: 60
    steps:
      - uses: actions/download-artifact@3e5f45b2cfb9172054b4087a40e8e0b5a5461e7c # v8.0.1
        with:
          path: ${{ github.workspace }}/artifacts
      - run: pip install cloudsmith-cli
      - name: Upload to Cloudsmith
        env:
          CLOUDSMITH_API_KEY: ${{ secrets.TEST_REPO_CLOUDSMITH_TOKEN }} # TODO(production): CLOUDSMITH_API_KEY
        run: |
          set -e
          VERSION="${{ needs.setup.outputs.cloudsmith-version }}"
          ART="${{ github.workspace }}/artifacts"
          for dir in "$ART"/*/; do
            tag=$(basename "$dir"); [[ "$tag" == "Artifact-manifest" ]] && continue
            if [[ "$tag" == Windows-* ]]; then
              (cd "$ART" && zip -r "$tag.zip" "$tag" && rm -r "$tag" \
               && cloudsmith push raw --republish --tags "$tag" --version "$VERSION" adi/test-alin-repo "$tag.zip" \
               && rm "$tag.zip"); continue   # TODO(production): adi/external
            fi
            for f in "$dir"*; do
              [[ -f "$f" ]] || continue
              fn=$(basename "$f"); stable=$(echo "$fn" | sed 's/\.g[0-9a-f]\+//')
              cloudsmith push raw --republish --tags "$tag" --version "$VERSION" adi/test-alin-repo "$f" # TODO(production): adi/external
            done
          done
```

- [ ] **Step 3: `push-github-release`** (draft, tags only)

```yaml
  push-github-release:
    if: needs.setup.outputs.is-release == 'true'
    needs: [setup, check-artifacts]
    runs-on: ubuntu-latest
    timeout-minutes: 10
    permissions:
      contents: write
    steps:
      - uses: actions/download-artifact@3e5f45b2cfb9172054b4087a40e8e0b5a5461e7c # v8.0.1
        with:
          path: ${{ github.workspace }}/artifacts
      - uses: softprops/action-gh-release@3bb12739c298aeb8a4eeaf626c5b8d85266b0e65 # v2.6.2
        with:
          draft: true
          name: "iio-oscilloscope ${{ github.ref_name }}"
          files: ${{ github.workspace }}/artifacts/*
          generate_release_notes: true
```

- [ ] **Step 4: Lint**

Run: `"$HOME/.tools/actionlint/actionlint.exe" .github/workflows/build.yml`
Expected: exit 0.

- [ ] **Step 5: Commit**

```bash
git add .github/workflows/build.yml
git commit -m "ci: add check-artifacts, Cloudsmith push, and draft GitHub release"
```

---

### Task 8: `scheduled.yml` — monthly v0 dispatch

**Files:**
- Create: `.github/workflows/scheduled.yml`

- [ ] **Step 1: Write it**

```yaml
name: Scheduled Build
on:
  schedule:
    - cron: '7 3 1 * *'   # Monthly, 1st ~03:07 UTC
permissions:
  actions: write
jobs:
  trigger:
    runs-on: ubuntu-latest
    timeout-minutes: 10
    steps:
      - name: Dispatch build
        run: gh workflow run build.yml --ref main   # O2: nightly branch
        env:
          GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}
```

- [ ] **Step 2: Lint + commit**

Run: `"$HOME/.tools/actionlint/actionlint.exe" .github/workflows/scheduled.yml`
```bash
git add .github/workflows/scheduled.yml
git commit -m "ci: add monthly scheduled build dispatch"
```

---

### Task 9: Remove legacy standalone workflows

**Files:**
- Delete: `.github/workflows/buildmingw.yml`
- Delete: `.github/workflows/build_ubuntu_appimage.yml`

- [ ] **Step 1: Confirm coverage before deleting**

`buildmingw.yml` is superseded by `_windows_builds.yml`; `build_ubuntu_appimage.yml` logic now lives in `_arm_builds.yml` (`arm-native`). Confirm both covered.

- [ ] **Step 2: Delete + lint remaining set**

```bash
git rm .github/workflows/buildmingw.yml .github/workflows/build_ubuntu_appimage.yml
"$HOME/.tools/actionlint/actionlint.exe" .github/workflows/*.yml
```
Expected: exit 0 across all remaining workflows.

- [ ] **Step 3: Commit**

```bash
git commit -m "ci: remove legacy standalone build workflows"
```

---

### Task 10: Hardening pass — SHA pins, trailing newlines, full lint

**Files:**
- Modify: any workflow still using a tag-ref action; ensure trailing newline on every workflow + relocated script.

- [ ] **Step 1: Find unpinned actions**

Run: `grep -rnE "uses: .*@v[0-9]" .github/workflows/`
Expected: empty. Pin any match to the SHAs in "Pinned action SHAs" (or resolve via `gh api`).

- [ ] **Step 2: Ensure trailing newlines**

For each workflow and relocated script, confirm the file ends with a newline; add one if missing.

- [ ] **Step 3: Full-tree lint**

Run: `"$HOME/.tools/actionlint/actionlint.exe" .github/workflows/*.yml`
Expected: exit 0, zero findings.

- [ ] **Step 4: Final trace against the spec**

Confirm: no `CI/azure` references; artifact names consistent between producers and `check-artifacts`/`push-cloudsmith`; `windows` not passed `build-type`; all `# TODO(production)` markers present where test refs are used.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "ci: pin actions to SHAs and finalize workflow hygiene"
```

---

## Self-Review notes

- **Spec coverage:** §6.2 orchestrator → Tasks 6/7; §6.3 Linux → Task 2; §6.4 ARM → Task 3; §6.5 macOS → Task 4; §6.6 Windows → Task 5; §6.7 scheduled → Task 8; §7 scripts → Tasks 1/4; §8 removals → Tasks 1/9; hardening → Task 10. All sections mapped.
- **Review Focus:** empty-artifact gating (Task 7 Step 1), artifact-name↔package match (Tasks 2/3 traces), bare-container basics incl. `sudo` (Tasks 2/3 base-package steps), reusable-input ordering (inputs in Tasks 2–4 precede wiring in Task 6), QEMU `fail-fast: false` (Task 3).
- **No local build execution:** every task's gate is `actionlint` + manual trace; real build/publish verification happens only when CI runs after a future push (out of scope here).
