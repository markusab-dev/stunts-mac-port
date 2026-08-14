#!/usr/bin/env python3
"""
Inventory and hashing tool for original Stunts game files.
Computes SHA-256 hashes, categorizes files, extracts DOS MZ headers and version signatures.
"""

import os
import sys
import json
import hashlib
import zipfile
import struct
from pathlib import Path

WORKSPACE = Path(__file__).resolve().parent.parent
ORIGINAL_DIR = WORKSPACE / "Original"
EXTRACTED_DIR = WORKSPACE / "extracted" / "stunts"
DOCS_DIR = WORKSPACE / "docs"

def sha256_file(filepath: Path) -> str:
    h = hashlib.sha256()
    with open(filepath, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def inspect_dos_exe(filepath: Path) -> dict:
    info = {"is_dos_exe": False}
    try:
        with open(filepath, "rb") as f:
            data = f.read(1024)
            if len(data) >= 2 and data[:2] == b"MZ" or data[:2] == b"ZM":
                info["is_dos_exe"] = True
                e_cblp, e_cp, e_crlc, e_cparhdr, e_minalloc, e_maxalloc, e_ss, e_sp, e_csum, e_ip, e_cs, e_lfarlc, e_ovno = struct.unpack("<13H", data[2:28])
                header_size = e_cparhdr * 16
                load_module_size = (e_cp * 512) - ((512 - e_cblp) % 512)
                file_size = filepath.stat().st_size
                info["header_paragraphs"] = e_cparhdr
                info["header_bytes"] = header_size
                info["reloc_entries"] = e_crlc
                info["reloc_table_offset"] = e_lfarlc
                info["initial_cs_ip"] = f"{e_cs:04X}:{e_ip:04X}"
                info["initial_ss_sp"] = f"{e_ss:04X}:{e_sp:04X}"
                info["min_extra_paragraphs"] = e_minalloc
                info["max_extra_paragraphs"] = e_maxalloc
                info["calculated_exe_size"] = load_module_size
                info["actual_file_size"] = file_size
                info["has_overlay_data"] = file_size > load_module_size
                info["overlay_size"] = max(0, file_size - load_module_size)
    except Exception as e:
        info["error"] = str(e)
    return info

def categorize_file(filename: str) -> str:
    fn = filename.upper()
    ext = os.path.splitext(fn)[1]
    
    if ext in [".EXE", ".COM", ".BAT"]:
        return "Executable / Launcher"
    elif ext in [".DRV", ".COD", ".DIF", ".HDR"]:
        return "Graphics / Display Driver / Code Overlay"
    elif ext in [".SFX", ".VCE", ".PLB", ".MID", ".ADL", ".SND"]:
        return "Audio / Sound / Music Resource"
    elif ext in [".TRK"]:
        return "Track Definition"
    elif ext in [".HIG"]:
        return "Track High Scores"
    elif ext in [".RPL"]:
        return "Replay Recording"
    elif ext in [".P3S", ".3SH", ".3SD"]:
        return "3D Model / Car / Object Geometry"
    elif ext in [".PVS", ".PRE", ".PES"]:
        return "Pack / Resource Container (Packed/Compressed)"
    elif ext in [".RES"]:
        return "Resource Archive / Index"
    elif ext in [".FNT"]:
        return "Bitmap Font"
    elif ext in [".DAT", ".CFG"]:
        return "Configuration / Setup Data"
    elif ext in [".KMS"]:
        return "Audio / Sequence / Music Data"
    elif ext in [".HTM", ".HTML", ".TXT", ".DOC", ".ICO", ".JPG"]:
        return "Documentation / Icon / Metadata"
    else:
        return "Other / Data"

def main():
    print(f"Workspace: {WORKSPACE}")
    DOCS_DIR.mkdir(parents=True, exist_ok=True)
    EXTRACTED_DIR.mkdir(parents=True, exist_ok=True)
    
    # Find original zip
    zip_files = list(ORIGINAL_DIR.glob("*.zip"))
    if not zip_files:
        print(f"Error: No zip files found in {ORIGINAL_DIR}")
        sys.exit(1)
        
    zip_path = zip_files[0]
    zip_hash = sha256_file(zip_path)
    zip_size = zip_path.stat().st_size
    print(f"Original ZIP: {zip_path.name} ({zip_size} bytes)")
    print(f"SHA-256: {zip_hash}")
    
    # Extract
    with zipfile.ZipFile(zip_path, 'r') as zf:
        zf.extractall(EXTRACTED_DIR.parent)
    
    # The zip contains stunts/stunts/...
    # Let's locate all extracted files
    extracted_files = []
    for root, dirs, files in os.walk(EXTRACTED_DIR.parent):
        for f in files:
            full_path = Path(root) / f
            rel_path = full_path.relative_to(EXTRACTED_DIR.parent)
            extracted_files.append((full_path, str(rel_path)))
            
    print(f"Extracted {len(extracted_files)} files.")
    
    inventory = {
        "original_archive": {
            "filename": zip_path.name,
            "path": str(zip_path.relative_to(WORKSPACE)),
            "size_bytes": zip_size,
            "sha256": zip_hash
        },
        "file_count": len(extracted_files),
        "files": []
    }
    
    categories = {}
    
    for full_path, rel_path in sorted(extracted_files, key=lambda x: x[1]):
        size = full_path.stat().st_size
        sha = sha256_file(full_path)
        cat = categorize_file(full_path.name)
        categories[cat] = categories.get(cat, 0) + 1
        
        file_info = {
            "name": full_path.name,
            "relative_path": rel_path,
            "size_bytes": size,
            "sha256": sha,
            "category": cat
        }
        
        if full_path.suffix.upper() in [".EXE", ".COM"]:
            exe_info = inspect_dos_exe(full_path)
            if exe_info.get("is_dos_exe"):
                file_info["dos_exe_info"] = exe_info
                
        inventory["files"].append(file_info)
        
    inventory["category_summary"] = categories
    
    # Write JSON
    json_path = DOCS_DIR / "original_inventory.json"
    with open(json_path, "w") as f:
        json.dump(inventory, f, indent=2)
    print(f"Wrote machine-readable inventory to {json_path}")
    
    print("\nSummary by category:")
    for cat, count in sorted(categories.items()):
        print(f"  {cat}: {count} files")

if __name__ == "__main__":
    main()
