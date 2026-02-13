# Trusted Shuffler

This repo contains the necessary elements to run Cobalt's Trusted Shuffler.

## Set up

### Bazel

This project uses a specific version of Bazel (defined in [.bazelversion](.bazelversion)). It is 
recommended to use [Bazelisk](https://github.com/bazelbuild/bazelisk) to manage your Bazel versions.

## Running the shuffler

To build and run the shuffler, use the following command:

```bash
bazel run //src:main
```