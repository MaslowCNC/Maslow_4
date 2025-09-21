# Comment Relay Workflow

This document explains how the relay comments to copilot workflow works.

## Purpose

The `relay-comments-to-copilot.yml` workflow enables users to get help from @copilot by mentioning @MaslowBot in their comments. Since copilot only responds to comments from users who have the subscription (MaslowBot), this workflow automatically relays user requests to copilot.

## How It Works

1. **Trigger**: The workflow triggers when:
   - Someone creates a comment on an issue
   - Someone creates a review comment on a pull request

2. **Conditions**: The workflow only runs if:
   - The comment mentions `@MaslowBot` or `@maslowbot` (case-insensitive)
   - The commenter is NOT MaslowBot itself (prevents infinite loops)

3. **Processing**: Before relaying, the workflow checks:
   - If the comment already mentions `@copilot` (skips to avoid duplicates)
   - If the comment is too short (< 10 characters, skips minimal mentions)

4. **Relay**: If all conditions pass, MaslowBot posts a new comment that:
   - Addresses `@copilot` directly
   - Credits the original author
   - Includes the original comment content
   - Adds a footer explaining it was automatically relayed

## Example Usage

**User posts:**
```
@MaslowBot can you help me understand why my Z-axis is not working correctly?
```

**MaslowBot automatically posts:**
```
@copilot 

User @username mentioned @MaslowBot with the following request:

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

@MaslowBot can you help me understand why my Z-axis is not working correctly?

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

_This comment was automatically relayed by MaslowBot so that @copilot can respond to the request._
```

## Security & Permissions

- Uses `USER_GITHUB_TOKEN` secret for authentication
- Only creates comments, doesn't modify existing content
- Includes safety checks to prevent abuse or infinite loops

## Supported Triggers

- Issue comments (`issue_comment` event)
- Pull request review comments (`pull_request_review_comment` event)