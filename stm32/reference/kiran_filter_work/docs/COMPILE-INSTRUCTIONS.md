# How to Compile Your Resume

## Quick Start (Recommended)

### Option 1: Overleaf (Online - No Installation Needed)
1. Go to [overleaf.com](https://overleaf.com)
2. Create a new project
3. Copy-paste the content from `resume-kiran-simple.tex`
4. Click "Recompile"

### Option 2: Install LaTeX Locally

**macOS:**
```bash
# Install MacTeX (full distribution)
brew install --cask mactex
# OR minimal installation
brew install --cask basictex
# Update texlive
sudo tlmgr update --self --all
```

**Ubuntu/Debian:**
```bash
sudo apt-get install texlive-xetex texlive-fonts-recommended texlive-fonts-extra texlive-latex-extra
```

**Windows:**
- Download [TeX Live](https://www.tug.org/texlive/) or [MiKTeX](https://miktex.org/)

### Compile the Resume

```bash
# Navigate to the project directory
cd /Users/kiran/Projects/airport-ebpf-mac

# Compile (runs twice for table of contents and cross-references)
pdflatex resume-kiran-simple.tex
pdflatex resume-kiran-simple.tex

# Open the generated PDF
open resume-kiran-simple.pdf
```

## Files Provided

| File | Description |
|------|-------------|
| `resume-kiran.tex` | Full featured version with icons (requires fontawesome package) |
| `resume-kiran-simple.tex` | **RECOMMENDED** - Simplified version, compiles everywhere |
| `resume-kiran-simple.pdf` | Generated PDF (after compilation) |

## What's Included

✅ All sections from your original resume  
✅ No content cutoff - flows naturally across pages  
✅ Clean, professional formatting  
✅ Hyperlinked URLs and emails  
✅ Proper spacing and typography  
✅ All project details including eBPF defense system  

## Troubleshooting

**Error: "Package fontawesome5 not found"**
- Use `resume-kiran-simple.tex` instead (doesn't need fontawesome)

**Error: "pdflatex not found"**
- Install LaTeX using the instructions above

**Content still cutoff?**
- The simple version uses `\par\nobreak\vskip\smallskipamount` which allows page breaks
- No hardcoded `\smallskip` commands that block page breaks
