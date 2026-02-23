"""File browser endpoint for selecting local files/directories."""

import os
import string
from pathlib import Path

from fastapi import APIRouter, Query

router = APIRouter(prefix="/api/browse", tags=["browse"])


def _get_drives() -> list[dict]:
    """List available Windows drive letters."""
    drives = []
    for letter in string.ascii_uppercase:
        drive = f"{letter}:\\"
        if os.path.exists(drive):
            drives.append({"name": f"{letter}:", "path": drive, "is_dir": True})
    return drives


@router.get("")
async def browse_directory(
    path: str = Query("", description="Directory path to list. Empty = drives/root."),
    filter_ext: str = Query("", description="Comma-separated extensions to show (e.g. '.exe,.bat'). Empty = all."),
):
    """List contents of a directory for file browser UI."""
    # If no path, return drive letters (Windows) or root
    if not path:
        if os.name == "nt":
            return {"status": "success", "path": "", "parent": "", "entries": _get_drives()}
        else:
            path = "/"

    p = Path(path)
    if not p.exists():
        return {"status": "error", "message": f"Path does not exist: {path}"}
    if not p.is_dir():
        return {"status": "error", "message": f"Not a directory: {path}"}

    # Parse extension filter
    exts = set()
    if filter_ext:
        for ext in filter_ext.split(","):
            ext = ext.strip().lower()
            if not ext.startswith("."):
                ext = f".{ext}"
            exts.add(ext)

    entries = []
    try:
        for item in sorted(p.iterdir(), key=lambda x: (not x.is_dir(), x.name.lower())):
            # Skip hidden files/dirs
            if item.name.startswith("."):
                continue

            is_dir = item.is_dir()

            # Apply extension filter only to files
            if not is_dir and exts:
                if item.suffix.lower() not in exts:
                    continue

            entries.append({
                "name": item.name,
                "path": str(item),
                "is_dir": is_dir,
            })
    except PermissionError:
        return {"status": "error", "message": f"Permission denied: {path}"}

    # Parent directory
    parent = str(p.parent) if p.parent != p else ""

    return {
        "status": "success",
        "path": str(p),
        "parent": parent,
        "entries": entries,
    }
