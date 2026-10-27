<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="assets/github_header_dark.png">
    <source media="(prefers-color-scheme: light)" srcset="assets/github_header_light.png">
    <img src="assets/github_header_light.png" alt="XLibrary — Application launcher and organizer for Windows and Linux" width="100%">
  </picture>
</p>

<p align="center"><strong>Your apps, organized your way.</strong></p>

<p align="center">
  <a href="https://github.com/Lappland-Julia/XLibrary/actions/workflows/windows-build.yml"><img src="https://github.com/Lappland-Julia/XLibrary/actions/workflows/windows-build.yml/badge.svg" alt="Windows build"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-68717C" alt="License: MIT"></a>
  <a href="https://github.com/Lappland-Julia/XLibrary/actions/workflows/linux-build.yml"><img src="https://github.com/Lappland-Julia/XLibrary/actions/workflows/linux-build.yml/badge.svg" alt="Linux build"></a>
  <img src="https://img.shields.io/badge/platform-Windows%20%2F%20Linux-68717C" alt="Platform: Windows and Linux">
</p>

<p align="center">
  <a href="#features">Features</a> ·
  <a href="#search">Search</a> ·
  <a href="#development">Development</a> ·
  <a href="#contributing">Contributing</a> ·
  <a href="https://github.com/Lappland-Julia/XLibrary/issues">Feedback</a>
</p>

## XLibrary

XLibrary is an open-source application launcher and organizer for Windows and Linux. Bring your everyday tools, games, and portable apps into one library, organize them with tags, and launch them from a single place.

An app can belong to more than one part of your day. Give your browser both `#work` and `#personal`, keep development tools under `#dev`, or create a launch scenario that opens everything you need for a project.

> **Under active development.** Features and interfaces are still evolving. Early feedback and contributions are welcome.

<p align="center">
  <h3 align="center"><a href="https://github.com/Lappland-Julia/XLibrary/releases">Download Latest Release</a></h3>
</p>

## Features

| | What you can do |
| --- | --- |
| **One application library** | Add apps manually or discover them through the Windows registry and Start menu, or Linux XDG desktop entries. |
| **Tags and filters** | Assign multiple tags to an app and combine them with text search and exclusions. |
| **Favorites and recent launches** | Keep frequently used apps close and sort your library by last launch. |
| **Running apps** | See what is running, switch to an existing window when available, or open a new instance. |
| **Launch scenarios** | Open a group of apps in a defined order, with optional delays between steps. |
| **Desktop integration** | Use a global hotkey, keep XLibrary in the system tray, and customize keyboard shortcuts. |
| **Appearance** | Choose a light, dark, or system theme and adjust the text size. |
| **Local storage** | Keep your catalog in SQLite. Import and export application entries, tags, and favorites as JSON. |

## Import and discovery

JSON format 1 stores application names, absolute paths, launch arguments, working directories,
favorites, and assigned tags. Linux executable paths do not need a filename extension. Optional
fields may be omitted; supplied fields must have the expected JSON types. Import validates the
entire file before writing and reports the record number, field, and reason on invalid input.
An import error leaves the catalog unchanged. Files are limited to 5 MiB.

Discovery keeps a report in the library and in Settings → Library. Open **Last scan details** to
read or copy it. The summary separates additions, duplicates, skipped entries, and errors.
Duplicate names or normalized paths are skipped; existing entries keep their metadata.
Cancelling discovery before its transaction is committed rolls back all additions. Once commit
starts, cancellation is unavailable. JSON import and discovery never launch applications.

JSON export is an application catalog transfer, not a complete profile backup: scenarios,
unused tags, settings, and launch history are not included.

## Updates

Open **Settings → About → Check for updates** to query this project's GitHub Releases.
You can optionally enter your own GitHub Personal Access Token in the same section.
For this public repository, a token without additional permissions is sufficient.
Click **Save token** to store it in Windows Credential Manager or the Linux secret store
(Secret Service/KWallet); **Delete token** restores unauthenticated checks. These two
buttons take effect immediately, independently of the settings dialog's Save/Cancel buttons.
The token is sent only to `api.github.com`, including subsequent release-list pages, and
is never attached to installer downloads. It is not written to `settings.ini` or catalog exports.
If the secret store is unavailable or locked, XLibrary reports the error and never falls back
to plaintext storage. A saved token belongs to the OS user and local profile path; enter it
again after moving a portable profile. Without a saved token, checks work as before.

Checks run only on request and include published pre-releases. Versions are compared using
Semantic Versioning; drafts and tags without a supported version are ignored.

On Windows x64, an installer-managed copy can download the matching MinGW or MSVC installer
after confirmation. XLibrary verifies its size and GitHub-provided SHA-256 before starting
the installation wizard in the existing installation directory, then exits normally. The
wizard completes the update and offers to reopen XLibrary; the library and settings stay
in the existing profile. Downloads can be cancelled; closing Settings cancels an active request.

Portable copies, Linux installations, and releases without a matching installer and SHA-256
open the release in your browser after confirmation. Follow its update instructions and keep
your portable data folder; on Linux, use the appropriate package or AppImage workflow.
No downloads or installations happen in the background.

## Search

Search by app name or executable filename, then narrow the results with your own tags.

| Query | Result |
| --- | --- |
| `code` | Apps with “code” in their name or filename |
| `#dev` | Apps tagged `dev` |
| `#work #chat` | Apps with both tags |
| `#work -#games` | Apps tagged `work`, excluding those tagged `games` |
| `#dev code` | Apps tagged `dev` that also match “code” |

Press <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>Space</kbd> to bring up the library, or <kbd>Ctrl</kbd> + <kbd>K</kbd> to focus search. Both shortcuts can be changed in settings. On Wayland, assign the global shortcut in your desktop settings to the `XLibrary` command.

For the selected application:

| Shortcut | Action |
| --- | --- |
| <kbd>Enter</kbd> | Launch or switch to the running application, according to the launch settings |
| <kbd>Ctrl</kbd> + <kbd>Enter</kbd> | Launch a new instance |
| <kbd>Alt</kbd> + <kbd>Enter</kbd> | Open the application folder |
| <kbd>Ctrl</kbd> + <kbd>Shift</kbd> + <kbd>C</kbd> | Copy the application path |
| <kbd>Delete</kbd> | Remove the entry from the library after confirmation; application files are kept |

Change or clear these bindings in Settings → Keyboard shortcuts. Both main and numeric keypad Enter are supported. Text editing keys keep their usual behavior in text fields. Use ↑/↓ in search to select an application; Escape clears the search and cannot be reassigned. Duplicate shortcuts are rejected when saving settings.

## Development

XLibrary is built with **C++23**, **Qt 6 Widgets**, and **SQLite**. It targets **Windows 10/11 and Linux**. The interface supports English and Russian. It follows the system's preferred supported language by default and falls back to English. Choose a language in Settings → General and restart XLibrary to apply it. The application translation is embedded in the executable.

Building from source requires a C++23 compiler, **CMake 3.21+**, and **Qt 6.5+** with the Widgets, Concurrent, and Network modules, and the LinguistTools build tools (plus DBus on Linux). CMake downloads SQLite during the initial configuration. See [Building on Windows and Linux](BUILDING.md) for CLion presets, GCC/Clang and MSVC/MinGW builds, portable packages, installers, and release verification.

Planned work includes a compact launcher mode, further search improvements, and extensibility. These are development directions rather than a release schedule.

## Contributing

Help shape a library you would want to use every day. Bug reports, interface feedback, documentation improvements, and code contributions are all welcome.

- **Report a bug:** [open an issue](https://github.com/Lappland-Julia/XLibrary/issues/new) with steps to reproduce, your OS and desktop session, and screenshots where useful.
- **Suggest an improvement:** [share your idea](https://github.com/Lappland-Julia/XLibrary/issues) and describe the workflow it would help with.
- **Contribute code:** submit a pull request with a focused change. Please discuss larger features in an issue first.

If you would like to follow the project, star the repository or watch it for updates.

## License

XLibrary is released under the [MIT License](LICENSE).
