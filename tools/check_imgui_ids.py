#!/usr/bin/env python3
import os
import re
import sys

WIDGETS = [
    "Button", "Checkbox", "InputText", "InputTextMultiline", "InputTextWithHint",
    "SliderFloat", "SliderInt", "Combo", "BeginChild", "CollapsingHeader", "Begin",
    "Selectable", "RadioButton", "DragFloat", "DragInt", "InputFloat", "InputInt",
    "InputDouble", "ListBox", "TreeNode", "TreeNodeEx", "ProgressBar",
]

LABEL_RE = re.compile(r'ImGui::(' + "|".join(WIDGETS) + r')\([^"\n]*?"([^"]*)"')
PUSH_ID_RE = re.compile(r'ImGui::PushID\(')
POP_ID_RE = re.compile(r'ImGui::PopID\(')


def scan_file(path: str) -> int:
    issues = 0
    with open(path, "r", encoding="utf-8", errors="ignore") as handle:
        lines = handle.readlines()

    push_balance = 0
    for idx, line in enumerate(lines, 1):
        if PUSH_ID_RE.search(line):
            push_balance += 1
        if POP_ID_RE.search(line) and push_balance > 0:
            push_balance -= 1

        for match in LABEL_RE.finditer(line):
            label = match.group(2)
            if not label:
                continue
            if "##" in label or "###" in label:
                # Caller is responsible for a PushID in loops if needed.
                continue
            if push_balance == 0:
                print(f"{path}:{idx}: possible ID collision risk for label '{label}'")
                issues += 1

    return issues


def main() -> int:
    root = sys.argv[1] if len(sys.argv) > 1 else "src"
    total = 0
    for dirpath, _, filenames in os.walk(root):
        for name in filenames:
            if not name.endswith((".cpp", ".h", ".mm", ".c", ".cc")):
                continue
            total += scan_file(os.path.join(dirpath, name))
    if total:
        print(f"Found {total} potential ImGui ID issues.")
        return 1
    print("No potential ImGui ID issues found.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
