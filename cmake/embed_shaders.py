#!/usr/bin/env python3
import sys
from pathlib import Path

def escape_for_cpp(s: str) -> str:
    s = s.replace("\\", "\\\\")
    s = s.replace("\"", "\\\"")
    s = s.replace("\n", "\\n\"\n\"")
    return s

def main():
    if len(sys.argv) < 3:
        print("usage: embed_shaders.py <out_header> <shader files...>", file=sys.stderr)
        sys.exit(1)
    out_path = Path(sys.argv[1])
    shader_files = [Path(p) for p in sys.argv[2:]]
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", encoding="utf-8") as f:
        f.write("#pragma once\n")
        f.write("#include <string_view>\n")
        f.write("namespace keys { namespace shaders {\n")
        for path in shader_files:
            name = path.name.replace(".", "_")
            content = path.read_text(encoding="utf-8")
            content = escape_for_cpp(content)
            f.write(f"inline constexpr std::string_view {name} = \"{content}\";\n")
        f.write("}}\n")

if __name__ == "__main__":
    main()
