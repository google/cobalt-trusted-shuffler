# Trusted Shuffler

This repo contains the necessary elements to run Cobalt's Trusted Shuffler.

## Set up

### Bazel

This project uses a specific version of Bazel (defined in [.bazelversion](.bazelversion)). It is
recommended to use [Bazelisk](https://github.com/bazelbuild/bazelisk) to manage your Bazel versions.

### Additional libraries

The following dependencies are also needed for development:

-  libxml2

They can be installed through apt-get:

```bash
sudo apt-get install libxml2
```

## Running the shuffler

To build and run the shuffler, use the following command:

```bash
bazel run //src:main
```

## Running tests

To run the shuffler tests, use the following command:

```bash
bazel test //...
```

## Regenerating compile comands

To regenerate `compile_commands.json`, use the following command:

```bash
bazel run //:refresh_compile_commands
```

## Submitting code

In order for a CL to be submitted, the Kokoro presubmit bot must
successfully build the CL patchset and post a `Presubmit-Verified+1`
vote. The presubmit bot typically takes under 5 minutes to start
a build after a new patchset is uploaded, and completes in 10 to
15 minutes after starting. The bot will post a comment on the CL
when it starts building, with a link to the live build log if
you want to monitor how the build is progressing. After the build
completes, it will comment again on the CL as well as post
either a `Presubmit-Verified+1` or `Presubmit-Verified-1` vote
depending on if the build was successul or failed.

If you want to submit the CL without waiting for a
`Presubmit-Verified+1` vote, you can append a tag to the bottom
of the CL description in the format: `PresubmitVerifiedOverride:
<reason>`. This can be useful for instance in emergency situations
where you can't wait ~15 minutes for the presubmit bot to trigger
and run, if the presubmit service itself is broken, or if you need
to temporarily break the build on purpose.

