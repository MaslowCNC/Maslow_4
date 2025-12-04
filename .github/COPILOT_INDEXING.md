# GitHub Copilot Repository Indexing Guide

This document explains how GitHub Copilot indexes this repository and how to ensure you're getting the best code suggestions.

## How Copilot Indexing Works (2024+)

GitHub Copilot automatically indexes repositories for semantic code search and context-aware suggestions. **There is no manual setup or re-indexing trigger needed.**

### Automatic Indexing Process

1. **Initial Index**: When you first open Copilot Chat in the context of this repository (either on GitHub.com or in your IDE), Copilot automatically creates a semantic index of the codebase
   - Initial indexing typically takes less than 60 seconds
   - This happens automatically—no action needed from you

2. **Keeping Index Updated**: Once indexed, Copilot keeps the index fresh
   - When you start a new Copilot Chat session, the index refreshes almost instantly
   - The index updates to reflect your latest code changes
   - This ensures suggestions always reference your current codebase

### How to "Refresh" the Index

If you want Copilot to use the latest repository state:

**On GitHub.com:**
- Simply open a new Copilot Chat session in the repository
- The index automatically refreshes to the latest code

**In VS Code:**
- Open Copilot Chat (@workspace or repository context)
- The index updates automatically when you start a conversation
- If needed, you can restart VS Code to force a fresh index

**In Other IDEs:**
- Start a new Copilot Chat session with repository context
- The indexing happens automatically

### Troubleshooting

If Copilot doesn't seem to be using recent changes:

1. **Start a Fresh Chat Session**: Close and reopen Copilot Chat
2. **Restart Your IDE**: Sometimes a full IDE restart helps
3. **Clear IDE Cache**: Some IDEs cache Copilot data—clearing it can help
4. **Check Indexing Limits**: 
   - Copilot Individual: Up to 5 repositories
   - Copilot Business: Up to 50 repositories
   - Copilot Enterprise: Unlimited repositories

If you've hit your indexing limit, Copilot may not index new repositories until you delete an old one or upgrade your plan.

## Repository Structure

This repository is organized as follows:

### Primary Source Directories

- **`firmware/FluidNC/src/`** - Main C++ firmware source code (311 files)
  - Core CNC control logic
  - Machine configurations and kinematics
  - Motor control and spindle drivers
  - Web UI backend
  - Maslow-specific implementations

- **`firmware/`** - ESP32 CNC firmware based on FluidNC
  - Embedded web UI build system
  - Platform-specific configurations
  - Build scripts and tools

- **`docs/`** - User and assembly documentation
  - Assembly instructions
  - User guides
  - Wiki content

### Primary Languages

- **C/C++**: 311 source files in `firmware/FluidNC/src/`
  - Follow `.clang-format` style guide
  - ESP32-specific embedded code
  
- **Python**: 8 files for build scripts and tools
  - PlatformIO build automation
  - Web UI build system
  - Release packaging

- **JavaScript/HTML/CSS**: Web UI files in `firmware/embedded/www/`

### Build Artifacts (Ignored)

The following directories contain build artifacts and are properly ignored:
- `.pio/` - PlatformIO build cache
- `build/` - General build output
- `firmware/embedded/node_modules/` - Node.js dependencies
- `firmware/embedded/dist/` - Built web assets

These are excluded via `.gitignore` and won't affect Copilot indexing.

## Linter and Language Configurations

This repository includes the following configurations to help with code intelligence:

### C/C++ Configuration

- **`.clang-format`**: Defines C++ code style
  - 140 character column limit
  - Specific brace wrapping rules
  - Alignment preferences
  - All C++ files should follow this format

### Universal Configuration

- **`.editorconfig`**: Cross-editor settings
  - Ensures final newlines in all files
  - Basic editor consistency

### Code Review Configuration

- **`.coderabbit.yaml`**: Automated code review settings
  - Enabled tools: cppcheck, shellcheck, yamllint, eslint, markdownlint
  - Multiple language linters configured

## Copilot Instructions

The repository includes a comprehensive `.github/copilot-instructions.md` file that provides:
- Build system commands and timing expectations
- Project structure navigation
- Development best practices
- Testing and validation procedures
- Common development tasks

These instructions help Copilot provide better, context-aware suggestions specific to this FluidNC-based Maslow CNC firmware project.

## Best Practices for Using Copilot

1. **Start Conversations with Context**: Use `@workspace` or reference specific directories when asking questions
2. **Reference File Paths**: When discussing specific files, use their full paths from the repository root
3. **Mention Build System**: When discussing builds, mention you're using PlatformIO (`pio run -e wifi_s3`)
4. **Specify Target Platform**: This is ESP32-S3 firmware—mentioning this helps with suggestions
5. **Use Technical Terms**: Terms like "FluidNC", "Maslow CNC", "PlatformIO", "ESP32-S3" help Copilot understand the context

## Repository Default Branch

This repository's default branch should be automatically detected by GitHub. If you need to manually check or set it:

```bash
# Check current default branch
git remote show origin | grep "HEAD branch"

# Or check GitHub repository settings
# Settings → General → Default branch
```

## Additional Resources

- [GitHub Copilot Indexing Documentation](https://docs.github.com/en/copilot/concepts/context/repository-indexing)
- [Copilot Chat Context](https://docs.github.com/en/copilot/using-github-copilot/asking-github-copilot-questions-in-your-ide)
- [Repository Indexing Discussion](https://github.com/orgs/community/discussions/153841)

## Summary

**You don't need to do anything special to refresh Copilot's repository index.** Simply:
1. Open a new Copilot Chat session in the repository context
2. The index automatically updates to the latest code
3. Start asking questions or requesting code suggestions

The repository is properly configured with language hints (`.clang-format`, `.editorconfig`) and comprehensive Copilot instructions (`.github/copilot-instructions.md`) to ensure Copilot provides the best possible suggestions for this ESP32-based CNC firmware project.
