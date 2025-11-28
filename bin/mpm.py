#!/usr/bin/env python3
"""
Meshtastic Plugin Manager (MPM) build integration script.
"""
import os
import subprocess
import sys

try:
    Import("env")  # noqa: F821 - SCons/PlatformIO provides Import and env
    IS_PLATFORMIO = True
except:
    IS_PLATFORMIO = False


def generate_protobuf_files(
    proto_file, options_file=None, output_dir=None, nanopb_dir=None
):
    """
    Generate protobuf C++ files using nanopb.
    """
    # Resolve proto file path
    proto_file = os.path.abspath(proto_file)
    if not os.path.exists(proto_file):
        print(f"Error: Proto file not found: {proto_file}")
        return False

    # Get proto directory and filename
    proto_dir = os.path.dirname(proto_file)
    proto_basename = os.path.basename(proto_file)
    proto_name = os.path.splitext(proto_basename)[0]

    # Determine output directory
    if output_dir is None:
        output_dir = proto_dir
    else:
        output_dir = os.path.abspath(output_dir)

    # Create output directory if needed
    os.makedirs(output_dir, exist_ok=True)

    # Auto-detect options file if not specified
    if options_file is None:
        candidate_options = os.path.join(proto_dir, f"{proto_name}.options")
        if os.path.exists(candidate_options):
            options_file = candidate_options
            print(f"Auto-detected options file: {options_file}")
    elif options_file:
        options_file = os.path.abspath(options_file)
        if not os.path.exists(options_file):
            print(f"Warning: Options file not found: {options_file}")
            options_file = None

    # Always generate protobuf files
    print(f"Generating protobuf files from {proto_basename}...")

    # Use pipx to run nanopb_generator
    # We map the arguments to nanopb_generator's expected format
    cmd = [
        "pipx",
        "run",
        "--spec",
        "nanopb",
        "nanopb_generator",
        "-D", output_dir,
        "-I", proto_dir,
        "-S", ".cpp",
        # "-v", # Verbose might be too noisy
        proto_file,
    ]

    try:
        # Run in proto directory so nanopb can find the .options file
        # We use cwd argument instead of os.chdir to avoid thread-safety issues in SCons
        subprocess.run(cmd, cwd=proto_dir, check=True, capture_output=True, text=True)
        return True

    except subprocess.CalledProcessError as e:
        print(f"Error generating protobufs: {e}")
        if e.stdout:
            print(e.stdout)
        if e.stderr:
            print(e.stderr)
        return False

def scan_plugins(project_dir):
    """
    Scan for plugins in the project directory.
    
    Returns:
        List of tuples (plugin_name, plugin_path, src_path, proto_files)
    """
    plugins_dir_rel = os.path.join("src", "plugins")
    plugins_dir = os.path.join(project_dir, plugins_dir_rel)
    
    if not os.path.exists(plugins_dir):
        return []
    
    plugins = []
    plugin_dirs = [d for d in os.listdir(plugins_dir) 
                   if os.path.isdir(os.path.join(plugins_dir, d)) and not d.startswith('.')]
    
    for plugin_name in plugin_dirs:
        plugin_path = os.path.join(plugins_dir, plugin_name)
        src_path = os.path.join(plugin_path, "src")
        
        # Check if plugin has a src directory
        if not os.path.isdir(src_path):
            continue
        
        # Scan for .proto files recursively in the plugin directory
        proto_files = []
        for root, dirs, files in os.walk(plugin_path):
            # Skip hidden directories
            dirs[:] = [d for d in dirs if not d.startswith('.')]
            for file in files:
                if file.endswith('.proto'):
                    proto_files.append(os.path.join(root, file))
        
        plugins.append((plugin_name, plugin_path, src_path, proto_files))
    
    return plugins

def generate_all_protobuf_files(plugins, verbose=True):
    """
    Generate protobuf files for all proto files found in plugins.
    
    Args:
        plugins: List of plugin tuples from scan_plugins()
        verbose: Whether to print status messages
        
    Returns:
        Tuple of (success_count, total_count)
    """
    success_count = 0
    total_count = 0
    
    for plugin_name, plugin_path, src_path, proto_files in plugins:
        for proto_file in proto_files:
            total_count += 1
            proto_basename = os.path.basename(proto_file)
            proto_dir = os.path.dirname(proto_file)
            proto_name = os.path.splitext(proto_basename)[0]
            
            # Check for options file
            options_file = os.path.join(proto_dir, f"{proto_name}.options")
            options_path = options_file if os.path.exists(options_file) else None
            
            if verbose:
                print(f"MPM: Processing {proto_basename} from {plugin_name}...")
            
            if generate_protobuf_files(proto_file, options_path, proto_dir, None):
                success_count += 1
            elif verbose:
                print(f"MPM: Failed to generate protobuf files for {proto_basename}")
    
    return success_count, total_count

def init_plugins(env, projenv=None):
    """
    Scan for plugins, update build filters, and handle protobuf generation.
    
    Args:
        env: Build environment (for SRC_FILTER and protobuf actions)
        projenv: Project environment (for CPPPATH - include paths)
    """
    project_dir = env["PROJECT_DIR"]
    plugins_dir_rel = os.path.join("src", "plugins")
    
    # Use projenv if provided, otherwise fall back to env
    include_env = projenv if projenv is not None else env

    env.Append(SRC_FILTER=[f"-<{plugins_dir_rel}/*>"])

    print(f"MPM: Scanning plugins in {plugins_dir_rel}...")
    
    plugins = scan_plugins(project_dir)
    
    if not plugins:
        print(f"MPM: No plugins directory found at {plugins_dir_rel}")
        return
    
    for plugin_name, plugin_path, src_path, proto_files in plugins:
        print(f"MPM: Found plugin {plugin_name}")
        
        # Update SRC_FILTER
        rel_src_path = os.path.relpath(src_path, project_dir)
        env.Append(SRC_FILTER=[f"+<{rel_src_path}/*>"])

        # Add plugin src to include paths (use projenv for compiler include paths)
        include_env.Append(CPPPATH=[src_path])
        print(f"MPM: Added include path {rel_src_path}")

        # Log proto files for this plugin
        for proto_file in proto_files:
            proto_basename = os.path.basename(proto_file)
            print(f"MPM: Registered proto {proto_basename} for {plugin_name}")
    
    # Generate protobuf files for all plugins
    generate_all_protobuf_files(plugins, verbose=True)

def find_project_dir():
    """
    Find the project directory (where platformio.ini is located).
    """
    current_dir = os.path.abspath(os.path.dirname(__file__))
    
    # Start from the script directory and walk up
    search_dir = current_dir
    while search_dir != os.path.dirname(search_dir):  # Stop at filesystem root
        platformio_ini = os.path.join(search_dir, "platformio.ini")
        if os.path.exists(platformio_ini):
            return search_dir
        search_dir = os.path.dirname(search_dir)
    
    # Fallback: assume we're in bin/ directory
    return os.path.dirname(current_dir)

if __name__ == "__main__":
    # Standalone execution: scan plugins, show available ones, and generate protobuf files
    print("MPM: Running in standalone mode")
    
    project_dir = find_project_dir()
    print(f"MPM: Project directory: {project_dir}")
    
    plugins = scan_plugins(project_dir)
    
    if not plugins:
        print("MPM: No plugins found")
        sys.exit(0)
    
    print(f"\nMPM: Found {len(plugins)} plugin(s):")
    for plugin_name, plugin_path, src_path, proto_files in plugins:
        print(f"  - {plugin_name}")
        if proto_files:
            for proto_file in proto_files:
                proto_rel = os.path.relpath(proto_file, project_dir)
                print(f"    └─ {proto_rel}")
    
    print("\nMPM: Generating protobuf files for all plugins...")
    success_count, total_count = generate_all_protobuf_files(plugins, verbose=True)
    
    print(f"\nMPM: Completed - {success_count}/{total_count} protobuf file(s) generated successfully")
    
    if success_count < total_count:
        sys.exit(1)

# PlatformIO integration: When this script is imported by PlatformIO (not run as __main__),
# initialize plugins for the build system
if IS_PLATFORMIO and __name__ != "__main__":
    try:
        init_plugins(env)  # noqa: F821 - env is provided by PlatformIO/SCons
    except NameError:
        pass
