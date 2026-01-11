# JamWide Rename Integration Plan

This plan covers the one-step rename of the project from NINJAM CLAP to JamWide,
including namespace changes, targets, bundle IDs, and documentation updates.

## Decisions

- Backward compatibility is not required.
- Namespace will be renamed from `ninjam` to `jamwide`.
- AUv2 codes: Manufacturer `JMWD`, Subtype `JWAU`, Type `aufx` — ✅ Already done.
- Bundle identifier: `com.jamwide.client` — ✅ Already done.

## Checklist

### 0) Prep
- [x] Create checkpoint tag: `v0.94-pre-rename`
- [ ] Ensure no uncommitted work is at risk.
- [ ] Confirm final identifiers and AUv2 codes.

### 1) GitHub + Local
- [ ] Rename GitHub repo to `JamWide`.
- [ ] Update local remote:
  - `git remote set-url origin git@github.com:mkschulze/JamWide.git`
- [ ] Rename local folder (optional but recommended):
  - `mv ninjam-clap JamWide`

### 2) CMake & Targets
- [ ] `CMakeLists.txt`: `project(ninjam-clap ...)` -> `project(jamwide ...)`
- [ ] Options: `NINJAM_CLAP_*` -> `JAMWIDE_*`
- [ ] Targets:
  - `ninjam-impl` -> `jamwide-impl`
  - `ninjam-ui` -> `jamwide-ui`
  - `ninjam-threading` -> `jamwide-threading`
  - `ninjam_clap` -> `jamwide_clap`
- [ ] `OUTPUT_NAME "NINJAM"` -> `"JamWide"`
- [ ] Bundle IDs: `com.ninjam.*` -> `com.jamwide.*`
- [ ] Update `SetFile` post-build and install targets to new target names.

### 3) CLAP Descriptor
- [ ] `src/plugin/clap_entry.cpp`:
  - `.id` -> `com.jamwide.client`
  - `.name` -> `JamWide`
  - `.vendor` -> `JamWide`
  - `.url` / `.manual_url` / `.support_url` -> GitHub URL
- [ ] `src/plugin/clap_entry_export.cpp`: Update any NINJAM comments

### 4) Namespace + Plugin Struct
- [ ] Rename namespace `ninjam` -> `jamwide` across `src/`.
- [ ] Rename `NinjamPlugin` -> `JamWidePlugin`.
- [ ] Rename header:
  - `src/plugin/ninjam_plugin.h` -> `src/plugin/jamwide_plugin.h`
- [ ] Update all includes accordingly.

### 5) Logging
- [ ] `src/debug/logging.h`: `/tmp/ninjam-clap.log` -> `/tmp/jamwide.log`
- [ ] Update any log references in `memory-bank/*.md`.

### 6) Scripts
- [ ] `install.sh`: `NINJAM.clap` -> `JamWide.clap`, update messages.
- [ ] `release.sh`: update labels/messages.

### 7) Info.plist / Bundle
- [ ] `resources/Info.plist.in`: bundle name/identifier/display name to JamWide.

### 8) Build Number
- [ ] `src/build_number.h`: `NINJAM_BUILD_NUMBER` -> `JAMWIDE_BUILD_NUMBER`
- [ ] Update references in `install.sh`, `release.sh`, `ui_status.cpp`.

### 9) Tools
- [x] `tools/check_imgui_ids.py`: No NINJAM references — no changes needed.

### 10) Documentation
- [ ] `README.md`: title, URLs, references.
- [ ] `memory-bank/*.md`: rename textual references.
- [ ] `memory-bank/ninjam.code-workspace`: Keep filename as-is.

### 11) Sanity
- [ ] Clean build directory.
- [ ] Reconfigure CMake (old cache uses previous option names).
- [ ] Build CLAP/VST3/AUv2.

## Execution Sequence (Suggested)

0) Checkpoint already created
```
v0.94-pre-rename tag pushed
```

1) Rename repo and local folder (if desired)
```
git remote set-url origin git@github.com:mkschulze/JamWide.git
mv /Users/cell/dev/ninjam-clap /Users/cell/dev/JamWide
```

2) Update CMake and target names
```
rg -n "ninjam-clap|NINJAM_CLAP_|ninjam-impl|ninjam-ui|ninjam-threading|ninjam_clap" CMakeLists.txt
```

3) Namespace + plugin struct rename
```
mv src/plugin/ninjam_plugin.h src/plugin/jamwide_plugin.h
rg -n "ninjam_plugin.h|NinjamPlugin|\\bninjam\\b" src
```

4) Update CLAP descriptor
```
rg -n "com\\.ninjam|NINJAM|cockos\\.com/ninjam" src/plugin/clap_entry.cpp
```

6) Update logging path
```
rg -n "ninjam-clap\\.log" src memory-bank
```

7) Update scripts
```
rg -n "NINJAM\\.clap|NINJAM CLAP|ninjam" install.sh release.sh
```

8) Update docs
```
rg -n "NINJAM|ninjam-clap|com\\.ninjam" README.md memory-bank
```

9) Clean and rebuild
```
rm -rf build
cmake -S . -B build
cmake --build build
```
