# Release automation

## Tag-driven releases

Pushing a semver tag that matches `v*.*.*` runs `.github/workflows/release.yml` and publishes a GitHub Release automatically.

```bash
git tag v1.21.0
git push origin v1.21.0
```

The workflow builds the production firmware target list from `release-manifest.json`, validates the required release inputs, packages the release artifacts, generates checksums and release notes, and then creates or updates the GitHub Release.

## Manual reruns with workflow_dispatch

The workflow also supports `workflow_dispatch`.

- Leave `tag` empty when you manually run the workflow from an existing semver tag ref.
- Provide `tag` (for example `v0.0.0-ci-test`) when you want to rerun or test a specific tag from the Actions UI.

## Adding or removing a firmware target

Update `release-manifest.json`.

1. Add or remove the target entry in `targets`.
2. Define the staged input globs the packaging step must validate.
3. Set the canonical output filename templates and any compatibility aliases.
4. If the target needs different packaging behavior, extend `scripts/package_release.py` to teach the zip builder how to include it.

The workflow matrix is generated from the manifest, so adding a new target there automatically updates the build job list.

## Troubleshooting

- **Missing staged artifacts**: check the `Validate staged inputs` log lines in the packaging job. The workflow prints every required glob and fails fast if one is missing.
- **Release assets not updating on rerun**: reruns reuse the same tag and replace existing assets with `gh release upload --clobber` after deleting stale assets.
- **Release notes look incomplete**: the notes generator prefers git history and commit metadata. Make sure the workflow has full tag history (`fetch-depth: 0` and `fetch-tags: true`).
- **Web UI mismatch**: the workflow rebuilds `ESP3D-WEBUI/dist/index.html.gz` and copies it into `firmware/FluidNC/data/index.html.gz` before `buildfs` so the filesystem image and standalone UI asset stay aligned.
