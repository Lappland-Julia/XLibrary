<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="assets/github_header_dark.png">
    <source media="(prefers-color-scheme: light)" srcset="assets/github_header_light.png">
    <img src="assets/github_header_light.png" alt="XLibrary — Application launcher and organizer for Windows" width="100%">
  </picture>
</p>

<p align="center"><strong>Your apps, organized your way.</strong></p>

<p align="center">
  <a href="https://github.com/Lappland-Julia/XLibrary/actions/workflows/windows-build.yml"><img src="https://github.com/Lappland-Julia/XLibrary/actions/workflows/windows-build.yml/badge.svg" alt="Windows build"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-68717C" alt="License: MIT"></a>
  <img src="https://img.shields.io/badge/platform-Windows%2010%20%2F%2011-68717C" alt="Platform: Windows 10 and 11">
</p>

<p align="center">
  <a href="#features">Features</a> ·
  <a href="#search">Search</a> ·
  <a href="#development">Development</a> ·
  <a href="#contributing">Contributing</a> ·
  <a href="https://github.com/Lappland-Julia/XLibrary/issues">Feedback</a>
</p>

## XLibrary

XLibrary is an open-source application launcher and organizer for Windows. Bring your everyday tools, games, and portable apps into one library, organize them with tags, and launch them from a single place.

An app can belong to more than one part of your day. Give your browser both `#work` and `#personal`, keep development tools under `#dev`, or create a launch scenario that opens everything you need for a project.

> **Under active development.** Features and interfaces are still evolving. Early feedback and contributions are welcome.

## Features

| | What you can do |
| --- | --- |
| **One application library** | Add apps manually or discover them through the Windows registry and Start menu shortcuts. |
| **Tags and filters** | Assign multiple tags to an app and combine them with text search and exclusions. |
| **Favorites and recent launches** | Keep frequently used apps close and sort your library by last launch. |
| **Running apps** | See what is running, switch to an existing window when available, or open a new instance. |
| **Launch scenarios** | Open a group of apps in a defined order, with optional delays between steps. |
| **Desktop integration** | Use a global hotkey, keep XLibrary in the system tray, and customize keyboard shortcuts. |
| **Appearance** | Choose a light, dark, or system theme and adjust the text size. |
| **Local storage** | Keep your catalog in SQLite. Import and export application entries, tags, and favorites as JSON. |

## Search

Search by app name or executable filename, then narrow the results with your own tags.

| Query | Result |
| --- | --- |
| `code` | Apps with “code” in their name or filename |
| `#dev` | Apps tagged `dev` |
| `#work #chat` | Apps with both tags |
| `#work -#games` | Apps tagged `work`, excluding those tagged `games` |
| `#dev code` | Apps tagged `dev` that also match “code” |

Press <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>Space</kbd> to bring up the library, or <kbd>Ctrl</kbd> + <kbd>K</kbd> to focus search. Both shortcuts can be changed in settings.

## Development

XLibrary is built with **C++23**, **Qt 6 Widgets**, and **SQLite**. It currently targets **Windows 10 and 11**. The current application interface is in Russian.

Building from source requires a C++23 compiler, **CMake 3.21+**, and **Qt 6.5+** with the Widgets, Concurrent, and Network modules. CMake downloads SQLite during the initial configuration. See [Building and releasing on Windows](BUILDING.md) for CLion setup, MSVC/MinGW builds, portable packages, installers, and release verification.

Planned work includes a compact launcher mode, further search improvements, and extensibility. These are development directions rather than a release schedule.

## Contributing

Help shape a library you would want to use every day. Bug reports, interface feedback, documentation improvements, and code contributions are all welcome.

- **Report a bug:** [open an issue](https://github.com/Lappland-Julia/XLibrary/issues/new) with steps to reproduce, your Windows version, and screenshots where useful.
- **Suggest an improvement:** [share your idea](https://github.com/Lappland-Julia/XLibrary/issues) and describe the workflow it would help with.
- **Contribute code:** submit a pull request with a focused change. Please discuss larger features in an issue first.

If you would like to follow the project, star the repository or watch it for updates.

## License

XLibrary is released under the [MIT License](LICENSE).
