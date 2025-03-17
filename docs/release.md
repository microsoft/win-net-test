# Release

All active development occurs on GitHub in the `main` branch. When it is time to release, the code is forked into a
release branch where it is considered stable and will generally only receive servicing for security and bug fixes.
New releases may be created at any time.

## Branches

win-net-test has two primary types of branches:

* **Main** - Main is the active development branch, and receives security and bug fixes just the same as the release
    branches. However, it may experience breaking changes as we develop new features. For this reason, it is recommended
    that no dependencies be taken on the main branch.

* **Release** - Release branches only receive security and bug fixes, and are considered stable. There should be no
    breaking changes in these branches, and they can be used for stable products.

## Tags

Tags are used for actual releases. Tags are created from the release branches, and are used to mark the code at a
specific point in time. Tags are immutable, and should not be changed once created.

## Versioning

win-net-test uses [Semantic Versioning](https://semver.org/) for versioning releases.
The version number is defined in the release branch name and can also be found in the [version.json](../version.json)
file. SemVer specifies that:

- **Major** version changes indicate **breaking changes**. This means that code that worked in a previous major version
    may not work in the new major version. This is generally due to API changes, but can also be due to changes in
    behavior.

- **Minor** version changes indicate **new features**. This means that code that worked in a previous minor version will
    continue to work in the new minor version. This is generally due to new APIs or features being added.

- **Patch** version changes indicate **bug fixes**. This means that code that worked in a previous patch version will
    continue to work in the new patch version.

win-net-test uses the `release/(major).(minor)` naming convention for all release **branches**.
For example, the first official release branch will be `release/1.0`.

win-net-test then uses the `v(major).(minor).(patch)` naming convention for all **tags**.
For example, the first official release will be `v1.0.0`.

# Release Process

The following sections are generally for the maintainers of win-net-test.

## How to create a new release

It is time for a new release! 
The process is somewhat manual, here is the step-by-step breakdown.

### Bump the version numer

Bump up the version number in `version.json`, respecting semantic versionning.
Complete the pull request in the `main` branch.

### Create a release branch

Create a new `release/(major).(minor)` branch from `main` from the last commit to include in the new release.

### Create a new tag

From a command line in the `win-net-test` repos:

- `git fetch`
- `git checkout release/(major).(minor)`
- `git tag v(major).(minor).(patch)`
- `git push --tags`

### Gather artifacts

Wait for the CI to successfully complete its run on the `release/(major).(minor)` branch.
Then, download the following artifacts from the run summary:

- release_dev_artifacts_Release
- release_runtime_artifacts_Release_arm64
- release_runtime_artifacts_Release_x64

Locally, extract them and collect the inner archives:
- win-net-test.(major).(minor).(patch).nupkg
- fn-runtime-arm64.zip
- fn-runtime-x64.zip

## Create a new release in GitHub

- Goto the the [Release section](https://github.com/microsoft/win-net-test/releases)
- Draft a new release from the `release/(major).(minor)` branch
- Select the tag `v(major).(minor).(patch)`
- Clean up the release notes
- Attach the archives and the nuget package gathered previously
- Publish it! 🎉

## Publish the nugget package

Got to [win-net-test on Nuget.org](https://www.nuget.org/packages/win-net-test).
If you are not an owner yet, reach out to an owner for help.

- Copy your API key or create a new one if needed
- `nuget push .\win-net-test.(major).(minor).(patch).nupkg <api key> -Source https://api.nuget.org/v3/index.json`

[Detailed instructions for publishing a nuget package](https://learn.microsoft.com/en-us/nuget/quickstart/create-and-publish-a-package-using-visual-studio?tabs=nuget#publish-the-package)
