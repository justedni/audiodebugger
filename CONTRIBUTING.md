# Contributing to Audio Debugger

Thanks for your interest! Bug reports, fixes, and feature ideas are all welcome.

## Before you start

- **Small fixes** (typos, build fixes, obvious bugs): just open a pull request.
- **Bigger changes** (new features, API changes, file format changes): please open an issue first so we can discuss the approach. The project is still a work in progress and the API moves; this avoids wasted effort on both sides.
- Changes to the `.rpt` binary format must stay backward compatible with existing trace files (bump the version and keep the old reading path).

## Building and testing

- Follow the build instructions in the [README](README.md).
- Make sure the `Library`, `SamplePlugin` and `Standalone` targets still build.
- If your change touches the audio thread path, keep it real-time safe: no allocations, locks, or blocking I/O in `processBlock` and the code it calls.
- If you can, test with the SamplePlugin in a DAW and check that a recorded trace still opens and regenerates correctly.

## Pull requests

- Keep PRs focused: one topic per PR.
- Match the existing code style.
- Describe **what** the change does and **why**, and how you tested it.

## LLM-assisted contributions

Using AI coding assistants (Copilot, Claude, ChatGPT, etc.) is fine. However:

- **A human must review the code before submitting it.** You are the author of the PR: you should have read every line, understood it, built it, and tested it.
- You should be able to explain and defend the changes during review. "The AI wrote it" is not an answer to a review question.
- Please mention in the PR description if a significant part of the code was generated.
- PRs that look like unreviewed, bulk-generated output (large unrelated changes, code that doesn't build, hallucinated APIs) will be closed without detailed review.

The same applies to issues: please don't submit AI-generated bug reports you haven't reproduced yourself.

## License

By contributing, you agree that your contributions will be licensed under the project's license, [GPL-3.0](LICENSE).
