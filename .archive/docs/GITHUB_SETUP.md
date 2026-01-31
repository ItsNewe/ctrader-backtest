# GitHub Setup Guide

## Quick Start - Push to GitHub

### Step 1: Install Git
If you haven't already, install Git for Windows:
```powershell
# Option A: Using Chocolatey (if admin)
choco install git -y

# Option B: Download installer
# https://git-scm.com/download/win
```

### Step 2: Create Repository on GitHub
1. Go to https://github.com/new
2. Create a new repository named: `ctrader-backtest`
3. Choose settings:
   - **Description**: "High-performance C++ backtesting engine for cTrader with MetaTrader integration"
   - **Visibility**: Public (or Private if preferred)
   - **Do NOT initialize** with README, .gitignore, or license (we have them locally)
4. Click "Create repository"

### Step 3: Initialize & Push Locally

After creating the repo on GitHub, run these commands:

```powershell
cd c:\Users\user\Documents\ctrader-backtest

# Initialize git repository
git init

# Configure git (use your GitHub credentials)
git config user.name "Your Name"
git config user.email "your.email@example.com"

# Add all files
git add .

# Create initial commit
git commit -m "Initial commit: Complete C++ backtesting engine with cTrader and MetaTrader support"

# Add remote (replace YOUR_USERNAME)
git remote add origin https://github.com/YOUR_USERNAME/ctrader-backtest.git

# Push to GitHub
git branch -M main
git push -u origin main
```

### Step 4: Configure Remote with SSH (Optional, Recommended)
For secure authentication without passwords:

```powershell
# Generate SSH key (if you don't have one)
ssh-keygen -t ed25519 -C "your.email@example.com"
# Press Enter to save in default location
# Enter passphrase (can be blank)

# Add SSH key to GitHub
# 1. Copy the key: Get-Content ~/.ssh/id_ed25519.pub
# 2. Go to https://github.com/settings/keys
# 3. Click "New SSH key"
# 4. Paste the key and save

# Update remote to use SSH
git remote set-url origin git@github.com:YOUR_USERNAME/ctrader-backtest.git
```

## Repository Structure for GitHub

```
ctrader-backtest/
├── README.md                    # Main documentation
├── LICENSE                      # MIT License
├── PROJECT_OVERVIEW.md          # Project overview
├── BUILD_GUIDE.md               # Build instructions
├── METATRADER_INTEGRATION.md    # MT integration guide
├── COMPLETION_SUMMARY.md        # What was added
├── CMakeLists.txt               # Build configuration
├── .gitignore                   # Git ignore rules
├── build.sh                     # Linux/macOS build script
├── build.bat                    # Windows build script
├── include/                     # Header files
│   ├── backtest_engine.h
│   ├── ctrader_connector.h
│   └── metatrader_connector.h
├── src/                         # Source files
│   ├── main.cpp
│   ├── backtest_engine.cpp
│   ├── ctrader_connector.cpp
│   └── metatrader_connector.cpp
├── data/                        # Sample data (gitignored)
├── generated/                   # Protobuf files (gitignored)
└── build/                       # Build output (gitignored)
```

## GitHub README Badges (Optional)

Add these to the top of your README.md for visual appeal:

```markdown
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++17](https://img.shields.io/badge/C++-17-00599C?logo=cplusplus)](https://en.wikipedia.org/wiki/C%2B%2B17)
[![CMake](https://img.shields.io/badge/CMake-3.15+-064F8C?logo=cmake)](https://cmake.org/)
[![Platform: Windows/Linux/macOS](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey)](https://github.com)
```

## GitHub Issues & Discussions

Once your repo is on GitHub, you can:

1. **Enable Discussions**: Settings → Features → enable "Discussions"
2. **Add Topics**: Settings → add topics like:
   - `backtesting`
   - `trading`
   - `ctrader`
   - `metatrader`
   - `cpp`
   - `cmake`
   - `financial-engineering`

## GitHub Actions (CI/CD) - Optional

Create `.github/workflows/build.yml`:

```yaml
name: Build

on: [push, pull_request]

jobs:
  build:
    runs-on: ${{ matrix.os }}
    strategy:
      matrix:
        os: [ubuntu-latest, windows-latest, macos-latest]
    
    steps:
    - uses: actions/checkout@v2
    
    - name: Install Dependencies (Ubuntu)
      if: runner.os == 'Linux'
      run: |
        sudo apt-get update
        sudo apt-get install -y cmake build-essential
    
    - name: Install Dependencies (macOS)
      if: runner.os == 'macOS'
      run: brew install cmake
    
    - name: Configure
      run: |
        mkdir build
        cd build
        cmake -DCMAKE_BUILD_TYPE=Release ..
    
    - name: Build
      run: cmake --build build --config Release
```

## Protecting Main Branch (Optional)

Recommended for production readiness:

Settings → Branches → Add Rule:
- Branch name pattern: `main`
- Require pull request reviews: 1
- Require status checks to pass

## Release Creation

To create a release on GitHub:

```powershell
# Create a git tag
git tag -a v1.0.0 -m "Version 1.0.0: Initial release"

# Push tags to GitHub
git push origin --tags
```

Then on GitHub:
1. Go to "Releases"
2. Click "Create a release"
3. Select tag `v1.0.0`
4. Add release notes
5. Attach any compiled binaries (optional)

## Common Git Commands

```powershell
# Check status
git status

# View commit history
git log --oneline -10

# Create a new branch
git checkout -b feature/my-feature

# Switch branches
git checkout main

# Merge branch
git merge feature/my-feature

# Push changes
git push origin main

# Pull latest changes
git pull origin main

# Undo last commit (keep changes)
git reset --soft HEAD~1

# View differences
git diff

# Stash changes temporarily
git stash
git stash pop
```

## GitHub Pages (Optional Documentation)

To host documentation:

1. Settings → Pages
2. Source: `main` branch → `/root` directory
3. Add `index.md` at project root
4. GitHub automatically builds from Markdown

## Collaboration

For contributing:

1. **Fork** the repository
2. Create a **feature branch**
3. Commit changes
4. Push to your fork
5. Create a **Pull Request**
6. Await review and merge

## .gitignore Already Includes

✅ Build artifacts (build/, CMakeFiles/, *.o, *.exe)
✅ Generated files (*.pb.cc, *.pb.h)
✅ IDE files (.vscode, .vs, .idea)
✅ Data files (*.csv)
✅ Temporary files (*.tmp, *.log)
✅ System files (.DS_Store)

## Resources

- [GitHub Help](https://docs.github.com)
- [Git Documentation](https://git-scm.com/doc)
- [GitHub Desktop](https://desktop.github.com/) - GUI alternative
- [GitKraken](https://www.gitkraken.com/) - Advanced GUI

## After First Push

Your GitHub repo will show:
- All source code
- Full documentation
- Build instructions
- MIT License
- Issue tracker (for bug reports)
- Discussions (for questions)
- Wiki (optional, for additional docs)

That's it! Your project will be live on GitHub! 🎉
