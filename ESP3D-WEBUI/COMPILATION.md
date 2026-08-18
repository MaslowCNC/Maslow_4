# ESP3D WebUI Compilation Guide

This is a quick reference for compiling the ESP3D WebUI. For detailed development information, see [HOWTO-Test-Locally.md](HOWTO-Test-Locally.md).

## Quick Start

```bash
# 1. Install dependencies (one-time setup)
npm install

# 2. Build for English language (recommended)
gulp package --lang en

# 3. Output: dist/index.html.gz (~124KB)
```

## System Requirements

- **Node.js**: v20+ 
- **npm**: v10+
- **Python**: v3.12+ (for testing only)

## Build Commands

### Single Language (Recommended for ESP32)
```bash
gulp package --lang en      # English (~122KB)
gulp package --lang fr      # French (~122KB)
gulp package --lang es      # Spanish (~125KB)
gulp package --lang de      # German (~122KB)
# ... other languages available
```

### Multiple Languages (May be too large for ESP32)
```bash
gulp package                 # All languages (~150KB+)
```

### NPM Shortcuts
```bash
npm run build:en            # Build English only (monolithic)
npm run build               # Build all languages (monolithic)
npm run build:split:en      # Build English split (index.html.gz + app.js.gz)
npm run build:split         # Build all languages split
npm run start               # Build English + start test server
```

## Split Build (Reduced index.html.gz Size)

When `index.html.gz` grows too large for the ESP32 flash, use the split build to
produce a smaller HTML file alongside a separately-uploadable JavaScript bundle:

```bash
gulp package:split --lang en   # Recommended
npm run build:split:en         # Same via npm
```

**Output files** (both must be uploaded to the ESP32 filesystem):

| File | Contents | ~Size |
|------|----------|-------|
| `dist/index.html.gz` | HTML + CSS inlined (no JS) | ~44 KB |
| `dist/app.js.gz`     | All JavaScript             | ~110 KB |

The firmware automatically serves `app.js.gz` when the browser requests `app.js`,
so no firmware changes are required.

## Testing Your Build

⚠️ **Always test on actual hardware for reliable results.**

```bash
# Upload dist/index.html.gz to your ESP32 device
# Access via the ESP32's IP address
```

The local proxy server (fluidnc-web-sim.py) can introduce bugs and doesn't accurately represent how the UI behaves on actual hardware. For reliable testing, always upload to your ESP32 device.

## Automated Compilation

🤖 **Don't want to compile locally?** Use GitHub Actions:

### Option 1: Request @MaslowBot as Reviewer
1. Create a Pull Request
2. Add @MaslowBot as a reviewer  
3. Download "Updated UI.zip" from Actions tab

### Option 2: Comment Trigger
1. Comment "please build" or "/build" on any PR
2. Get downloadable build artifact with instructions

## Build Output

- **Compressed**: `dist/index.html.gz` (~122-125KB) ← Upload this to ESP32
- **Uncompressed**: `dist/index.html` (~535KB) ← For development only

## File Size Limits

ESP32 storage is limited:
- ✅ **Single language**: ~122-125KB (fits)
- ⚠️ **Multi-language**: ~150KB+ (may not fit)

## Troubleshooting

### Dependencies Missing
```bash
# Node.js/npm missing? Install from nodejs.org
node --version    # Check version
npm --version     # Check version

# Python dependencies missing?
pip3 install flask zeroconf websockets requests
```

### Build Warnings
JSHint ES6/ES8 warnings are **normal** and don't break the build.

### Build Too Large
Use single language: `gulp package --lang en`

If it is still too large, use the split build which keeps `index.html.gz` small
and moves all JavaScript to a separate `app.js.gz`:

```bash
gulp package:split --lang en
```

Both `dist/index.html.gz` and `dist/app.js.gz` must be uploaded to the ESP32.

## Language Availability

Full list of supported languages:
- `en` - English
- `fr` - French  
- `es` - Spanish
- `de` - German
- `it` - Italian
- `ja` - Japanese
- `pl` - Polish
- `ptbr` - Portuguese (Brazil)
- `ru` - Russian
- `tr` - Turkish  
- `uk` - Ukrainian
- `zh_CN` - Chinese (Simplified)
- `hu` - Hungarian

## Next Steps

- **Development**: See [HOWTO-Test-Locally.md](HOWTO-Test-Locally.md)
- **Installation**: Upload `dist/index.html.gz` to your ESP32
- **Issues**: Create an issue or discussion on GitHub
- **Automated builds**: Use @MaslowBot reviewer for hands-off compilation