# Stale Issue and Pull Request Management

## Overview

This workflow automatically manages stale issues and pull requests to keep the repository clean and organized.

## How It Works

### Timeline

1. **Day 0**: An issue or pull request is created or last updated
2. **Day 30**: After 30 days of inactivity, the item is automatically labeled as `stale` and receives a notification comment
3. **Day 37**: If there's still no activity 7 days after the stale label, the item is automatically closed

### Criteria for Staleness

An issue or PR is considered stale when:
- It has had no activity (comments, commits, reviews, etc.) for 30 days
- It does NOT have any of the exempt labels (see below)

### Exempt Labels

The following labels will prevent issues/PRs from being marked as stale:
- `pinned` - For important items that should stay open
- `security` - For security-related issues that need ongoing attention
- `important` - For high-priority items

## Schedule

The workflow runs automatically:
- **Daily at midnight UTC (00:00)** - Checks all open issues and PRs
- **Manual trigger** - Can be run manually from the Actions tab if needed

## Required Workflow Permissions

The stale workflow needs these write permissions:
- `issues: write`
- `pull-requests: write`
- `actions: write` (required so `actions/stale` can update/reset its internal `_state` actions cache between runs)

## Removing the Stale Label

If an issue or PR marked as stale receives any activity (comment, commit, review), the `stale` label is automatically removed and the clock resets.

## Configuration

The workflow can be customized by editing `.github/workflows/stale.yml`:

- `days-before-issue-stale`: Days before marking issues as stale (default: 30)
- `days-before-issue-close`: Days after stale label before closing (default: 7)
- `days-before-pr-stale`: Days before marking PRs as stale (default: 30)
- `days-before-pr-close`: Days after stale label before closing (default: 7)
- `exempt-issue-labels`: Labels that prevent issues from going stale
- `exempt-pr-labels`: Labels that prevent PRs from going stale
- `ascending`: Process oldest items first (`true`) or newest first (`false`)

## Purpose

This workflow helps:
- Keep the repository organized by closing abandoned issues and PRs
- Clean up experimental branches that were never merged
- Reduce clutter from inactive items
- Allow contributors to focus on active work
- Provide a grace period for items that might still be relevant

## Reopening Closed Items

If an issue or PR was closed by the stale bot but is still relevant:
1. You can reopen it by clicking "Reopen" on the closed item
2. Or create a new issue/PR with updated information
