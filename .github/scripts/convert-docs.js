const fs = require('fs');
const path = require('path');
const { marked } = require('marked');

// Configure marked for GitHub Flavored Markdown
marked.setOptions({
  gfm: true,
  breaks: true,
});

// Supported file extensions for assets
const SUPPORTED_ASSETS = /\.(jpg|jpeg|png|gif|svg|pdf)$/i;

function htmlTemplate(title, content, relativePath) {
  // Calculate depth based on directory separators in the relative path
  const depth = relativePath ? relativePath.split(path.sep).filter(p => p).length - 1 : 0;
  const prefix = depth > 0 ? '../'.repeat(depth) : '';
  return `<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>${title} - Maslow CNC Documentation</title>
    <style>
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, sans-serif;
            max-width: 1000px;
            margin: 0 auto;
            padding: 20px;
            line-height: 1.6;
            color: #333;
        }
        img {
            max-width: 100%;
            height: auto;
            border-radius: 4px;
            margin: 10px 0;
        }
        h1, h2, h3, h4, h5, h6 {
            color: #2c3e50;
            margin-top: 24px;
            margin-bottom: 16px;
        }
        h1 {
            border-bottom: 2px solid #3498db;
            padding-bottom: 10px;
        }
        a {
            color: #3498db;
            text-decoration: none;
        }
        a:hover {
            text-decoration: underline;
        }
        code {
            background: #f4f4f4;
            padding: 2px 6px;
            border-radius: 3px;
            font-family: 'Monaco', 'Menlo', 'Ubuntu Mono', monospace;
        }
        pre {
            background: #f4f4f4;
            padding: 16px;
            border-radius: 4px;
            overflow-x: auto;
        }
        pre code {
            padding: 0;
        }
        blockquote {
            border-left: 4px solid #3498db;
            margin: 0;
            padding-left: 16px;
            color: #666;
        }
        table {
            border-collapse: collapse;
            width: 100%;
            margin: 16px 0;
        }
        table th, table td {
            border: 1px solid #ddd;
            padding: 8px 12px;
            text-align: left;
        }
        table th {
            background: #f8f9fa;
            font-weight: 600;
        }
        .nav-header {
            background: #f8f9fa;
            padding: 15px;
            border-radius: 5px;
            margin-bottom: 30px;
        }
        .nav-header a {
            margin-right: 15px;
        }
    </style>
</head>
<body>
    <div class="nav-header">
        <a href="${prefix}index.html">← Documentation Home</a>
        <a href="https://github.com/MaslowCNC/Maslow_4" target="_blank">GitHub</a>
        <a href="https://forums.maslowcnc.com/" target="_blank">Forums</a>
    </div>
    ${content}
</body>
</html>`;
}

function convertMarkdownFile(inputPath, outputPath, relativePath) {
  const markdown = fs.readFileSync(inputPath, 'utf8');
  const html = marked(markdown);

  // Extract title from first heading or use filename
  const titleMatch = markdown.match(/^#\s+(.+)$/m);
  const title = titleMatch ? titleMatch[1] : path.basename(inputPath, '.md');

  const fullHtml = htmlTemplate(title, html, relativePath);

  fs.mkdirSync(path.dirname(outputPath), { recursive: true });
  fs.writeFileSync(outputPath, fullHtml);
  console.log(`Converted: ${inputPath} -> ${outputPath}`);
}

function processDirectory(srcDir, destDir, baseDir = '') {
  const entries = fs.readdirSync(srcDir, { withFileTypes: true });

  for (const entry of entries) {
    const srcPath = path.join(srcDir, entry.name);
    const relativePath = path.join(baseDir, entry.name);

    if (entry.isDirectory()) {
      const destPath = path.join(destDir, entry.name);
      fs.mkdirSync(destPath, { recursive: true });
      processDirectory(srcPath, destPath, relativePath);
    } else if (entry.name.endsWith('.md')) {
      const outputName = entry.name === 'README.md' ? 'index.html' : entry.name.replace(/\.md$/, '.html');
      const destPath = path.join(destDir, outputName);
      convertMarkdownFile(srcPath, destPath, relativePath);
    } else if (SUPPORTED_ASSETS.test(entry.name)) {
      // Copy images and other assets
      const destPath = path.join(destDir, entry.name);
      fs.copyFileSync(srcPath, destPath);
      console.log(`Copied: ${srcPath} -> ${destPath}`);
    }
  }
}

// Convert all markdown files in docs directory
processDirectory('docs', '_site');
console.log('Conversion complete!');
