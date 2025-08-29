# Redirecting to safe upload script
Write-Host 'Redirecting to upload_data_safe.ps1 to prevent program overwriting...' -ForegroundColor Yellow
& '.\upload_data_safe.ps1' @args
