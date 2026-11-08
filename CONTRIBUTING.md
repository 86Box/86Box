# Contribution guidelines

The 86Box project welcomes contributions from anyone, as long as some basic guidelines are followed.

## Code pull request guidelines

* The usage of AI to generate parts of code is tolerated but must be disclosed within the PR description. Agent plans, work summaries, test instructions or other AI artifacts should be removed before submission.
* The submitted code must be compilable without any warnings and follow the established coding style, including copyright headers and logging practices.
* New emulated hardware should fall into the project scope, i.e. emulation of x86-based machines and associated ecosystem up to ~2000-01 hardware.
* Emulation of a piece of hardware should be self-contained within 86Box if possible and should implement the actual behavior of the real hardware rather than second-guessing from driver expectations.
  * Optional third party software dependencies are acceptable for external connectivity or host integration.
* ROM dumps must be provided with new emulated hardware and be added to our [ROM repository](https://github.com/86Box/roms)
* Changes that would break configurations created by the previous stable version must include a migration to allow older configurations to be loaded.
* Contributions that include changes to the user interface should include a visual demonstration of the change in the pull request description.
* Contributed components must be functionally complete. Do not submit half-done code with no intention to finish it to production quality.
* Introduction of new build-time dependencies must be accompanied with an update to the [project documentation](https://github.com/86Box/docs).

For major additions, contributors are expected to maintain the contributed code within reason.

## Emulated hardware request guidelines

In order to accept a request for new emulated hardware, the following criteria must be met:

* A ROM must be available and be added to [our ROM repository](https://github.com/86Box/roms)
* Documentation must be available or it must be feasible to reverse engineer with a reasonable amount of time and effort
* It must be feasible to implement with a reasonable amount of time and effort
* It has to fall inside the project's scope (we're talking about ~2000-01 hardware at the latest, really)

## Questions

If you're unsure about any aspect of contributing, don't hesitate to get in touch via any of our official communities linked in our [readme](README.md#community) or [GitHub Discussions](https://github.com/86Box/86Box/discussions).
