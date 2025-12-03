# Documentation Publishing Workflow

This workflow automatically publishes the markdown documentation from the `docs/` directory to GitHub Pages whenever a release is published.

## How It Works

The `publish-docs.yml` workflow:

1. **Triggers on release events** - Automatically runs when a new release is published
2. **Converts markdown to HTML** - Uses the `marked` library to convert all `.md` files to HTML
3. **Preserves structure** - Maintains the directory structure and copies all images
4. **Creates navigation** - Adds a navigation header to each page with links back to the home page
5. **Deploys to GitHub Pages** - Publishes the converted documentation as a static website

## Features

- **GitHub Flavored Markdown** support
- **Responsive design** with clean, professional styling
- **Automatic image handling** - Copies all image files (jpg, png, gif, svg)
- **Index page** - Creates a beautiful landing page with cards for each documentation section
- **Navigation** - Each page includes a header with links to GitHub, Forums, and the documentation home
- **README.md conversion** - Automatically converts README.md files to index.html for clean URLs

## Enabling GitHub Pages (One-Time Setup)

To enable this workflow, a repository administrator needs to configure GitHub Pages:

1. Go to the repository **Settings** tab
2. Navigate to **Pages** in the left sidebar
3. Under "Build and deployment", select:
   - **Source**: GitHub Actions
4. Save the settings

That's it! The next time a release is published, the documentation will automatically be deployed.

## Manual Deployment

You can also manually trigger the workflow without creating a release:

1. Go to the **Actions** tab
2. Select "Publish Documentation to GitHub Pages" workflow
3. Click "Run workflow"
4. Select the branch and click "Run workflow"

## Accessing the Published Documentation

After the workflow runs successfully, your documentation will be available at:

```
https://<username>.github.io/<repository-name>/
```

For this repository:
```
https://maslowcnc.github.io/Maslow_4/
```

## Workflow Structure

### Jobs

1. **build** - Converts markdown files to HTML and prepares the site for deployment
2. **deploy** - Deploys the built site to GitHub Pages

### Key Steps in the Build Job

1. Checkout the repository
2. Setup Node.js and install the `marked` package
3. Run the conversion script that:
   - Converts all `.md` files to `.html`
   - Converts `README.md` files to `index.html`
   - Copies all image files
   - Applies consistent styling and navigation
4. Create the homepage `index.html` with links to all documentation sections
5. Upload the site as an artifact

### Key Steps in the Deploy Job

1. Deploy the artifact to GitHub Pages
2. Provide the URL where the site is published

## Customization

### Styling

The HTML templates include embedded CSS. To customize the appearance, edit the `<style>` sections in:
- The `htmlTemplate` function (for individual pages)
- The index.html creation (for the homepage)

### Homepage Content

To update the documentation sections shown on the homepage, edit the doc-card divs in the index.html creation section of the workflow.

## Troubleshooting

### Workflow Fails

- Check the Actions tab for detailed error logs
- Ensure GitHub Pages is enabled in repository settings
- Verify that the `docs/` directory exists and contains markdown files

### Pages Not Updating

- Confirm the workflow completed successfully in the Actions tab
- GitHub Pages can take a few minutes to update
- Check that the deployment job completed and provided a URL

### Images Not Showing

- Ensure image paths in markdown use relative paths (e.g., `images/photo.jpg`)
- Verify image files are committed to the repository
- Check that image files have supported extensions (jpg, jpeg, png, gif, svg, pdf)

## Contributing

When adding new documentation:

1. Place markdown files in the `docs/` directory
2. Use relative paths for images: `![Description](images/image.jpg)`
3. Create subdirectories with `README.md` for sections with multiple pages
4. Commit and push your changes
5. Create a release to trigger publication

## Markdown Best Practices

- Use `# Heading` for the main title (will become the page title)
- Use relative image paths
- Structure content with clear headings
- Use blockquotes (`>`) for tips and important notes
- Use tables, code blocks, and lists as needed
- Test your markdown locally with a viewer before committing
