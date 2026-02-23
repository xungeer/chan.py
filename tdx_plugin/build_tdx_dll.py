import os
import re
import subprocess
import shutil

def extract_config(main_file):
    with open(main_file, "r", encoding="utf-8") as f:
        content = f.read()

    config = {}
    
    # Extract config_dict
    match = re.search(r'config_dict\s*=\s*\{([^}]+)\}', content)
    if match:
        dict_content = match.group(1)
        for line in dict_content.split('\n'):
            line = line.strip()
            if not line or line.startswith('#'): continue
            kv = line.split(':')
            if len(kv) >= 2:
                key = kv[0].strip().strip('\'"')
                val = kv[1].split(',')[0].strip()
                # Remove quotes for strings
                if val.startswith(('\'', '"')):
                    val = val.strip('\'"')
                config[key] = val
    return config

def generate_header(config, output_file):
    lines = [
        "#ifndef __TDX_CONFIG_H__",
        "#define __TDX_CONFIG_H__",
        "// Auto-generated from main.py config_dict",
        ""
    ]
    for k, v in config.items():
        if v == 'True':
            lines.append(f"#define CFG_{k.upper()} 1")
        elif v == 'False':
            lines.append(f"#define CFG_{k.upper()} 0")
        elif v == 'float("inf")':
            lines.append(f"#define CFG_{k.upper()} 999999.0f")
        elif v.replace('.','',1).isdigit():
            lines.append(f"#define CFG_{k.upper()} {v}")
        else:
            lines.append(f'#define CFG_{k.upper()} "{v}"')
            
    lines.extend(["", "#endif"])
    with open(output_file, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))

def build_dll():
    # Because we are now inside tdx_plugin, main.py is in the parent directory
    parent_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    main_py_path = os.path.join(parent_dir, "main.py")
    
    config = extract_config(main_py_path)
    
    header_path = "tdx_config.h"
    generate_header(config, header_path)
    print(f"Generated {header_path} with: {config}")
    
    # Compile with cl.exe locally in tdx_plugin directory
    cpp_file = "chan2026_main.cpp"
    dll_file = "chan2026.dll"
    
    # Needs Developer Command Prompt or vcvarsall.bat
    # Ensure compiling 32-bit (x86) DLL to avoid Tdx client crash
    vcvars_path = r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat"
    if not os.path.exists(vcvars_path):
        vcvars_path = r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"
        
    vcvars_cmd = f'"{vcvars_path}"' if os.path.exists(vcvars_path) else "vcvars32.bat"
    cmd = f'{vcvars_cmd} && cl.exe /LD /EHsc /O2 /I. /Fe:{dll_file} "{cpp_file}" user32.lib'
    print("Running:", cmd)
    
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, shell=True)
        print("STDOUT:", result.stdout)
        if result.stderr:
            print("STDERR:", result.stderr)
        
        if result.returncode == 0:
            print(f"Successfully built {dll_file}")
            
            # Copy dll to parent dir for easy access if needed
            copy_to_parent = os.path.join(parent_dir, dll_file)
            shutil.copy(dll_file, copy_to_parent)
            
            # Try to copy to tdx dir if possible (ignored if not found)
            tdx_dir = r"C:\new_tdx\T0002\dlls"
            if os.path.exists(tdx_dir):
                shutil.copy(dll_file, os.path.join(tdx_dir, "chan2026.dll"))
                print(f"Copied to {tdx_dir}")
        else:
            print("Failed to build DLL.")
    except Exception as e:
        print("Compilation error:", e)

if __name__ == "__main__":
    build_dll()
