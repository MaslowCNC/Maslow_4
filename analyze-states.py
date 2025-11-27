#!/usr/bin/env python3
"""
State Machine Analyzer for Maslow CNC

This script analyzes the source code to extract state definitions and transitions,
then compares them with the diagram files to detect discrepancies.

It can:
1. Extract states from Calibration.h (Maslow states)
2. Extract states from Types.h (FluidNC states)
3. Extract transitions from Calibration.cpp and Protocol.cpp
4. Compare with existing diagram files
5. Report discrepancies

Usage:
  python3 analyze-states.py                    # Check for discrepancies
  python3 analyze-states.py --verbose          # Show detailed analysis
  python3 analyze-states.py --update           # Update diagrams if needed (future)
"""

import argparse
import os
import re
import sys
from pathlib import Path
from typing import Dict, List, Set, Tuple


def find_project_root() -> Path:
    """Find the project root directory."""
    script_dir = Path(__file__).parent.resolve()
    # Check if we're in the project root
    if (script_dir / "FluidNC").exists():
        return script_dir
    # Check parent directories
    for parent in script_dir.parents:
        if (parent / "FluidNC").exists():
            return parent
    raise RuntimeError("Could not find project root (directory containing FluidNC)")


def extract_maslow_states(calibration_h_path: Path) -> Dict[str, int]:
    """Extract Maslow state definitions from Calibration.h"""
    states = {}
    
    if not calibration_h_path.exists():
        print(f"Warning: {calibration_h_path} not found")
        return states
    
    content = calibration_h_path.read_text()
    
    # Match #define STATE_NAME value patterns
    pattern = r'#define\s+([A-Z_]+)\s+(\d+)'
    
    for match in re.finditer(pattern, content):
        name = match.group(1)
        value = int(match.group(2))
        # Filter to only include known state names
        known_states = ['UNKNOWN', 'RETRACTING', 'RETRACTED', 'EXTENDING', 'EXTENDEDOUT',
                       'TAKING_SLACK', 'CALIBRATION_IN_PROGRESS', 'READY_TO_CUT',
                       'RELEASE_TENSION', 'CALIBRATION_COMPUTING']
        if name in known_states:
            states[name] = value
    
    return states


def extract_fluidnc_states(types_h_path: Path) -> Dict[str, int]:
    """Extract FluidNC state definitions from Types.h"""
    states = {}
    
    if not types_h_path.exists():
        print(f"Warning: {types_h_path} not found")
        return states
    
    content = types_h_path.read_text()
    
    # Find the State enum
    enum_match = re.search(r'enum\s+class\s+State\s*:\s*uint8_t\s*\{([^}]+)\}', content, re.DOTALL)
    if not enum_match:
        print("Warning: Could not find State enum in Types.h")
        return states
    
    enum_body = enum_match.group(1)
    
    # Parse enum values
    value = 0
    for line in enum_body.split('\n'):
        line = line.strip()
        if not line or line.startswith('//'):
            continue
        
        # Match "StateName," or "StateName = value," or "StateName, // comment"
        match = re.match(r'(\w+)\s*(?:=\s*(\d+))?\s*,?\s*(?://.*)?$', line)
        if match:
            name = match.group(1)
            if match.group(2):
                value = int(match.group(2))
            states[name] = value
            value += 1
    
    return states


def extract_maslow_transitions(calibration_cpp_path: Path) -> List[Tuple[str, str, str]]:
    """Extract state transitions from Calibration.cpp"""
    transitions = []
    
    if not calibration_cpp_path.exists():
        print(f"Warning: {calibration_cpp_path} not found")
        return transitions
    
    content = calibration_cpp_path.read_text()
    
    # Look for requestStateChange calls
    # Pattern: requestStateChange(STATE_NAME)
    pattern = r'requestStateChange\s*\(\s*([A-Z_]+)\s*\)'
    
    # Find all state change requests
    for match in re.finditer(pattern, content):
        target = match.group(1)
        transitions.append(("ANY", target, "requestStateChange"))
    
    # Look for currentState assignments
    # Pattern: currentState = STATE_NAME
    pattern = r'currentState\s*=\s*([A-Z_]+)'
    for match in re.finditer(pattern, content):
        target = match.group(1)
        transitions.append(("ANY", target, "direct assignment"))
    
    return transitions


def extract_fluidnc_transitions(protocol_cpp_path: Path) -> List[Tuple[str, str, str]]:
    """Extract state transitions from Protocol.cpp"""
    transitions = []
    
    if not protocol_cpp_path.exists():
        print(f"Warning: {protocol_cpp_path} not found")
        return transitions
    
    content = protocol_cpp_path.read_text()
    
    # Look for set_state calls
    # Pattern: sys.set_state(State::StateName)
    pattern = r'sys\.set_state\s*\(\s*State::(\w+)\s*\)'
    
    for match in re.finditer(pattern, content):
        target = match.group(1)
        transitions.append(("ANY", target, "set_state"))
    
    return transitions


def extract_diagram_states(diagram_path: Path) -> Set[str]:
    """Extract state names from a diagram file."""
    states = set()
    
    if not diagram_path.exists():
        return states
    
    content = diagram_path.read_text()
    
    # Extract states from different diagram formats
    if diagram_path.suffix == '.md':
        # Mermaid format: STATE_NAME --> OTHER_STATE
        pattern = r'([A-Z][A-Za-z_]+)\s*-->'
        for match in re.finditer(pattern, content):
            states.add(match.group(1))
        pattern = r'-->\s*([A-Z][A-Za-z_]+)'
        for match in re.finditer(pattern, content):
            states.add(match.group(1))
    
    elif diagram_path.suffix == '.dot':
        # DOT format: StateName [label="..."]
        pattern = r'^\s*([A-Z][A-Za-z_]+)\s*\['
        for match in re.finditer(pattern, content, re.MULTILINE):
            name = match.group(1)
            if name not in ['Boot', 'node', 'edge', 'graph', 'digraph']:
                states.add(name)
    
    elif diagram_path.suffix == '.diag':
        # blockdiag format: "StateName\n(id)" [...]
        pattern = r'"([A-Z][A-Za-z_]+)(?:\\n|\s)*\(\d+\)"'
        for match in re.finditer(pattern, content):
            states.add(match.group(1))
    
    return states


def compare_states(code_states: Dict[str, int], diagram_states: Set[str], diagram_name: str) -> List[str]:
    """Compare states from code with states from diagram."""
    issues = []
    
    code_state_names = set(code_states.keys())
    
    # Check for states in code but not in diagram
    missing_in_diagram = code_state_names - diagram_states
    if missing_in_diagram:
        issues.append(f"States in code but missing from {diagram_name}: {missing_in_diagram}")
    
    # Check for states in diagram but not in code
    # (Allow some extra states like Boot for entry points)
    extra_in_diagram = diagram_states - code_state_names - {'Boot', 'UNKNOWN'}
    if extra_in_diagram:
        issues.append(f"States in {diagram_name} but not in code: {extra_in_diagram}")
    
    return issues


def main():
    parser = argparse.ArgumentParser(
        description="Analyze state machines in Maslow CNC code and compare with diagrams",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--verbose", "-v", action="store_true", help="Show detailed analysis")
    parser.add_argument("--check-only", action="store_true", help="Only check, don't suggest updates")
    
    args = parser.parse_args()
    
    # Find project root
    try:
        project_root = find_project_root()
    except RuntimeError as e:
        print(f"Error: {e}")
        sys.exit(1)
    
    print("=" * 60)
    print("State Machine Analyzer for Maslow CNC")
    print("=" * 60)
    print(f"Project root: {project_root}")
    print()
    
    issues = []
    
    # === Analyze Maslow States ===
    print("=== Maslow State Machine ===")
    
    calibration_h = project_root / "FluidNC/src/Maslow/Calibration.h"
    calibration_cpp = project_root / "FluidNC/src/Maslow/Calibration.cpp"
    
    maslow_states = extract_maslow_states(calibration_h)
    print(f"Found {len(maslow_states)} Maslow states in code:")
    if args.verbose:
        for name, value in sorted(maslow_states.items(), key=lambda x: x[1]):
            print(f"  {value}: {name}")
    
    maslow_transitions = extract_maslow_transitions(calibration_cpp)
    print(f"Found {len(maslow_transitions)} state transitions in Calibration.cpp")
    
    # Check Maslow diagrams
    for diagram_file in ["state-diagram.md", "state-diagram.dot", "state-diagram.diag"]:
        diagram_path = project_root / "docs" / diagram_file
        diagram_states = extract_diagram_states(diagram_path)
        if diagram_states:
            print(f"Found {len(diagram_states)} states in {diagram_file}")
            diagram_issues = compare_states(maslow_states, diagram_states, diagram_file)
            issues.extend(diagram_issues)
    
    print()
    
    # === Analyze FluidNC States ===
    print("=== FluidNC State Machine ===")
    
    types_h = project_root / "FluidNC/src/Types.h"
    protocol_cpp = project_root / "FluidNC/src/Protocol.cpp"
    
    fluidnc_states = extract_fluidnc_states(types_h)
    print(f"Found {len(fluidnc_states)} FluidNC states in code:")
    if args.verbose:
        for name, value in sorted(fluidnc_states.items(), key=lambda x: x[1]):
            print(f"  {value}: {name}")
    
    fluidnc_transitions = extract_fluidnc_transitions(protocol_cpp)
    print(f"Found {len(fluidnc_transitions)} state transitions in Protocol.cpp")
    
    # Check FluidNC diagrams
    for diagram_file in ["fluidnc-state-diagram.md", "fluidnc-state-diagram.dot", "fluidnc-state-diagram.diag"]:
        diagram_path = project_root / "docs" / diagram_file
        diagram_states = extract_diagram_states(diagram_path)
        if diagram_states:
            print(f"Found {len(diagram_states)} states in {diagram_file}")
            diagram_issues = compare_states(fluidnc_states, diagram_states, diagram_file)
            issues.extend(diagram_issues)
    
    print()
    
    # === Report Results ===
    print("=" * 60)
    if issues:
        print("⚠️  DISCREPANCIES FOUND:")
        print()
        for issue in issues:
            print(f"  • {issue}")
        print()
        print("Please update the diagram files to match the source code.")
        print("Run 'python3 build-docs.py' after updating to regenerate images.")
        sys.exit(1)
    else:
        print("✅ All diagram files are consistent with source code.")
        sys.exit(0)


if __name__ == "__main__":
    main()
