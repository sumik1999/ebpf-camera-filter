# build.ps1 - PowerShell script for building Security Camera Image on Windows
# Usage: .\build.ps1

param(
    [switch]$Clean,
    [string]$ImageName = "airport-camera-builder",
    [string]$OutputDir = "$PSScriptRoot\..\..\output-images"
)

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)

Write-Host "📷 Building Security Camera Image for Raspberry Pi 4" -ForegroundColor Cyan
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host ""

# Create output directory
if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
    Write-Host "Created output directory: $OutputDir" -ForegroundColor Green
}

# Check if Docker is running
Write-Host "Checking Docker..." -ForegroundColor Yellow
$dockerStatus = docker info 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ Docker is not running. Please start Docker Desktop." -ForegroundColor Red
    Write-Host ""
    Write-Host "💡 To start Docker Desktop:" -ForegroundColor Yellow
    Write-Host "   1. Open Docker Desktop from Start Menu" -ForegroundColor White
    Write-Host "   2. Wait until the whale icon shows 'Docker Desktop is running'" -ForegroundColor White
    Write-Host "   3. Run this script again" -ForegroundColor White
    exit 1
}
Write-Host "✅ Docker is running" -ForegroundColor Green

# Clean build artifacts if requested
if ($Clean) {
    Write-Host "🧹 Cleaning build artifacts..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force "$ProjectRoot\buildroot\output" -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force "$ProjectRoot\output-images" -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
}

# Build the Docker image
Write-Host ""
Write-Host "🔨 Building Docker image..." -ForegroundColor Yellow
docker build -t $ImageName -f "$ProjectRoot\containers\Dockerfile" $ProjectRoot
if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ Docker build failed. See output above." -ForegroundColor Red
    exit 1
}
Write-Host "✅ Docker image built successfully" -ForegroundColor Green

# Run the build
Write-Host ""
Write-Host "🚀 Running build in container..." -ForegroundColor Yellow
docker run --rm -v "${ProjectRoot}:C:/build" -w "/build" $ImageName
if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ Build failed inside container. See output above." -ForegroundColor Red
    exit 1
}
Write-Host "✅ Build completed successfully" -ForegroundColor Green

# Extract output
Write-Host ""
Write-Host "📦 Extracting build output..." -ForegroundColor Yellow

# Use docker to copy files
docker run --rm -v "${ProjectRoot}:C:/build" -w "/build" $ImageName powershell -Command `
    "Copy-Item -Path 'C:/build/buildroot/output/images/*' -Destination 'C:/build/output-images/' -Force"

# Check if files exist
$fileCount = (Get-ChildItem "$OutputDir" -ErrorAction SilentlyContinue).Count
if ($fileCount -eq 0) {
    Write-Host "⚠️  No output files found. Check the Docker output above for errors." -ForegroundColor Yellow
} else {
    Write-Host "✅ Extracted $fileCount files to: $OutputDir" -ForegroundColor Green
}

# Show output
Write-Host ""
Write-Host "📋 Build Output:" -ForegroundColor Cyan
Get-ChildItem "$OutputDir" | Format-Table Name, Length, LastWriteTime -AutoSize

Write-Host ""
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host "✅ Build Complete!" -ForegroundColor Green
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "📖 See docs/reports/SECURITY-CAMERA-BUILD-REPORT.md for deployment instructions" -ForegroundColor White
Write-Host ""
Write-Host "Next Steps:" -ForegroundColor Yellow
Write-Host "  1. Download the output files to your Windows machine" -ForegroundColor White
Write-Host "  2. Flash rootfs.ext4 to SD card using Win32DiskImager" -ForegroundColor White
Write-Host "  3. Insert SD card into Raspberry Pi 4" -ForegroundColor White
Write-Host "  4. Power on and SSH into the device" -ForegroundColor White
Write-Host "     Default: ssh root@<pi-ip-address>" -ForegroundColor Gray
