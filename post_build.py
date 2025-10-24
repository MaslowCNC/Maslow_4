Import("env")
import os
import re

def after_build(source, target, env):
    """Display version information after successful build"""
    # Path to version.cpp file
    version_file = os.path.join(env.get("PROJECT_DIR"), "FluidNC", "src", "version.cpp")
    
    version_number = "unknown"
    
    # Try to read version from version.cpp
    if os.path.exists(version_file):
        try:
            with open(version_file, 'r') as f:
                content = f.read()
                # Extract VERSION_NUMBER value
                match = re.search(r'VERSION_NUMBER\s*=\s*"([^"]+)"', content)
                if match:
                    version_number = match.group(1)
        except Exception as e:
            print(f"Warning: Could not read version from {version_file}: {e}")
    
    # Print version information with a separator that matches PlatformIO style
    print("\n" + "=" * 80)
    print(f"Build Version: {version_number}")
    print("=" * 80)

# Add post-build action
env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", after_build)
