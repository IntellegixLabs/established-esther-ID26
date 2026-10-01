# GitHub and npm publishing

The repository is prepared to build native release assets and publish `established-esther`. Publishing is triggered by pushing a version tag; this project does not store or use an npm access token.

## One-time GitHub setup

1. Create a public GitHub repository and push this project to it.
2. Update the npm metadata locally and publish the first version once. Use browser-based npm login/2FA; do not paste a token into a file or chat:

	```sh
	npm login
	npm pkg set repository.url="https://github.com/OWNER/REPOSITORY.git" homepage="https://github.com/OWNER/REPOSITORY#readme" bugs.url="https://github.com/OWNER/REPOSITORY/issues"
	npm publish --access public
	```

3. On npmjs.com, open `established-esther` package settings and configure **Trusted Publishing** for your GitHub owner/repository and workflow file `.github/workflows/release.yml`. Allow direct publishing. Do not add an npm token to GitHub Actions secrets.
4. Confirm the package is public and the workflow has permission to publish with provenance.

The workflow's npm job writes the actual GitHub repository URL into package metadata before publishing. That URL is used by npm installs to find the matching GitHub Release asset. On the initial `v0.1.0` tag, the workflow creates the GitHub release and skips npm because you already published that version manually. Later version tags publish to both destinations through the configured trusted publisher.

## Publish a release

Keep the Git tag and `package.json` version aligned. For the initial `0.1.0` release:

```sh
git init
git add .
git commit -m "Prepare Established 0.1.0"
git branch -M main
git remote add origin https://github.com/OWNER/REPOSITORY.git
git push -u origin main
git tag v0.1.0
git push origin v0.1.0
```

Replace `OWNER/REPOSITORY` with the GitHub repository you created. The tag workflow builds Linux x64, macOS x64, macOS arm64, and Windows x64 binaries, runs the smoke tests, attaches the binaries to a GitHub Release, then publishes the npm package with provenance.

For the next release, update `version` in `package.json`, commit it, and push the corresponding `vX.Y.Z` tag.

## Install and use

After the workflow succeeds:

```sh
npm install --global established-esther
esther run program.esther
```

Or run without a global install:

```sh
npx established-esther run program.esther
```

The package downloads the native runtime during installation. Set `ESTHER_SKIP_DOWNLOAD=1` to skip that step when inspecting or packing the npm package locally.