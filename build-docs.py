#!/usr/bin/env python3
"""
Build documentation diagrams for the Maslow CNC project.

This script generates SVG, PDF, and PNG versions of the state diagram.
It supports both blockdiag and graphviz (dot) formats.

Requirements:
  - blockdiag: pip install blockdiag[pdf]
  - graphviz: apt-get install graphviz OR brew install graphviz

Usage:
  python3 build-docs.py           # Generate all formats from both sources
  python3 build-docs.py --help    # Show help
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path


# Commands that use -V instead of --version for version check
VERSION_FLAG_V_COMMANDS = {"dot", "neato", "fdp", "sfdp", "twopi", "circo"}


def check_command(cmd):
    """Check if a command is available in PATH."""
    try:
        # Use -V for graphviz commands, --version for others
        version_flag = "-V" if cmd in VERSION_FLAG_V_COMMANDS else "--version"
        result = subprocess.run([cmd, version_flag], capture_output=True)
        # Commands return exit code 0 on successful version check
        return result.returncode == 0
    except FileNotFoundError:
        return False


def run_command(cmd, cwd=None):
    """Run a command and return success status."""
    try:
        result = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"  Error: {result.stderr}")
            return False
        return True
    except Exception as e:
        print(f"  Error running command: {e}")
        return False


def generate_with_blockdiag(input_file, output_dir):
    """Generate diagrams using blockdiag."""
    print("\n=== Generating with blockdiag ===")

    if not check_command("blockdiag"):
        print("  blockdiag not found. Install with: pip install blockdiag[pdf]")
        return False

    base_name = Path(input_file).stem
    success = True

    # Generate SVG
    svg_output = output_dir / f"{base_name}.svg"
    print(f"  Generating SVG: {svg_output}")
    if not run_command(["blockdiag", "-Tsvg", str(input_file), "-o", str(svg_output)]):
        print("  Warning: blockdiag SVG generation failed (may be Pillow compatibility issue)")
        print("  Try: pip install 'pillow<10' or use graphviz instead")
        success = False

    # Generate PNG
    png_output = output_dir / f"{base_name}.png"
    print(f"  Generating PNG: {png_output}")
    if not run_command(["blockdiag", "-Tpng", str(input_file), "-o", str(png_output)]):
        print("  Warning: blockdiag PNG generation failed")
        success = False

    # Generate PDF
    pdf_output = output_dir / f"{base_name}.pdf"
    print(f"  Generating PDF: {pdf_output}")
    if not run_command(["blockdiag", "-Tpdf", str(input_file), "-o", str(pdf_output)]):
        print("  Warning: blockdiag PDF generation failed")
        success = False

    return success


def generate_with_graphviz(input_file, output_dir):
    """Generate diagrams using graphviz dot."""
    print("\n=== Generating with graphviz (dot) ===")

    if not check_command("dot"):
        print("  graphviz not found. Install with:")
        print("    Ubuntu/Debian: apt-get install graphviz")
        print("    macOS: brew install graphviz")
        print("    Windows: choco install graphviz")
        return False

    base_name = Path(input_file).stem
    success = True

    # Generate SVG
    svg_output = output_dir / f"{base_name}.svg"
    print(f"  Generating SVG: {svg_output}")
    if not run_command(["dot", "-Tsvg", str(input_file), "-o", str(svg_output)]):
        success = False

    # Generate PNG
    png_output = output_dir / f"{base_name}.png"
    print(f"  Generating PNG: {png_output}")
    if not run_command(["dot", "-Tpng", str(input_file), "-o", str(png_output)]):
        success = False

    # Generate PDF
    pdf_output = output_dir / f"{base_name}.pdf"
    print(f"  Generating PDF: {pdf_output}")
    if not run_command(["dot", "-Tpdf", str(input_file), "-o", str(pdf_output)]):
        success = False

    return success


def main():
    parser = argparse.ArgumentParser(
        description="Build documentation diagrams for Maslow CNC",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python3 build-docs.py                    # Generate all diagrams
  python3 build-docs.py --graphviz-only    # Use graphviz only
  python3 build-docs.py --blockdiag-only   # Use blockdiag only
  python3 build-docs.py --output-dir dist  # Output to dist directory
        """,
    )
    parser.add_argument("--graphviz-only", action="store_true", help="Only use graphviz (dot)")
    parser.add_argument("--blockdiag-only", action="store_true", help="Only use blockdiag")
    parser.add_argument("--output-dir", type=str, default="docs/diagrams", help="Output directory for generated files")

    args = parser.parse_args()

    # Get script directory and project root
    script_dir = Path(__file__).parent.resolve()
    docs_dir = script_dir / "docs"
    output_dir = script_dir / args.output_dir

    # Create output directory
    output_dir.mkdir(parents=True, exist_ok=True)

    print(f"Maslow CNC Documentation Builder")
    print(f"================================")
    print(f"Project root: {script_dir}")
    print(f"Output directory: {output_dir}")

    success = True

    # Define all diagram source files
    diagram_sources = [
        ("state-diagram", "Maslow State Machine"),
        ("fluidnc-state-diagram", "FluidNC State Machine"),
    ]

    for base_name, description in diagram_sources:
        blockdiag_file = docs_dir / f"{base_name}.diag"
        graphviz_file = docs_dir / f"{base_name}.dot"

        print(f"\n{'='*60}")
        print(f"Processing: {description}")
        print(f"{'='*60}")

        # Generate with blockdiag
        if not args.graphviz_only and blockdiag_file.exists():
            if not generate_with_blockdiag(blockdiag_file, output_dir):
                print("\n  Note: blockdiag may fail with Pillow >= 10.0")
                print("  Using graphviz as fallback...")

        # Generate with graphviz
        if not args.blockdiag_only and graphviz_file.exists():
            graphviz_success = generate_with_graphviz(graphviz_file, output_dir)
            if graphviz_success:
                success = True  # At least one method succeeded

    # Summary
    print("\n=== Summary ===")
    if success:
        print("Documentation diagrams generated successfully!")
        print(f"Output files are in: {output_dir}")
        for f in sorted(output_dir.iterdir()):
            print(f"  - {f.name}")
    else:
        print("Some diagrams could not be generated.")
        print("Please check the error messages above.")
        sys.exit(1)


if __name__ == "__main__":
    main()
