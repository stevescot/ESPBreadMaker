# Compatibility wrapper for upload_files_robust.ps1
# Redirects to the new unified upload.ps1 script
#
# This script maintains backward compatibility with existing usage patterns
# while providing the enhanced functionality of the unified upload script.

param(
    [string]$Directory = "./data",
    [string]$ServerIP = "192.168.250.125",
    [int]$DelayBetweenFiles = 500,
    [int]$MaxRetries = 3,
    [int]$RetryDelay = 2000,
    [switch]$PreserveDirectories,
    [string[]]$Files = @()
)

Write-Host "Note: upload_files_robust.ps1 is deprecated. Use upload.ps1 instead." -ForegroundColor Yellow
Write-Host "Redirecting to unified upload script..." -ForegroundColor Cyan

# Build arguments for the new script
$arguments = @()
$arguments += "-Directory", $Directory
$arguments += "-ServerIP", $ServerIP
$arguments += "-DelayBetweenFiles", $DelayBetweenFiles
$arguments += "-MaxRetries", $MaxRetries
$arguments += "-RetryDelay", $RetryDelay

if ($PreserveDirectories) {
    $arguments += "-PreserveDirectories"
}

if ($Files.Count -gt 0) {
    $arguments += "-Files", $Files
}

# Call the new unified script
& "$PSScriptRoot\upload.ps1" @arguments
