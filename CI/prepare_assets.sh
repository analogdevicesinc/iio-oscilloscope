#!/bin/bash -e

release_artifacts() {
    cd "${BUILD_ARTIFACTSTAGINGDIRECTORY}"

    for d in */; do
        d="${d%/}"
        [ -d "${d}" ] || continue
        if [ "${d}" = "Artifact-manifest" ]; then
            rm -r "${d}"
            continue
        fi
        if [ "$(find "${d}" -type f | wc -l)" -eq 1 ]; then
            # Single distributable (e.g. AppImage): publish the file as-is.
            find "${d}" -type f -exec mv {} ./ ";"
        else
            # Multi-file build tree: publish one zip.
            zip -qr "${d}.zip" "${d}"
        fi
        rm -r "${d}"
    done
}

case "${1:-}" in
    release)
        release_artifacts
        ;;
    *)
        echo "Usage: ${0} {release}" >&2
        exit 1
        ;;
esac
