#!/usr/bin/env pwsh
# Quick timing calculator to fix accidentally advanced stage

Write-Host "=== Stage Timing Recovery Calculator ===" -ForegroundColor Cyan
Write-Host ""

# Get current status
$status = curl -s "http://192.168.250.125/api/status" | ConvertFrom-Json

Write-Host "📊 CURRENT STATUS:" -ForegroundColor Yellow
Write-Host "   Program: $($status.programId)" -ForegroundColor White
Write-Host "   Stage: $($status.stage) (index $($status.stageIdx))" -ForegroundColor White
Write-Host "   Stage started: $(Get-Date -UnixTimeSeconds $status.actualStageStartTimes[5] -Format 'HH:mm:ss')" -ForegroundColor White
Write-Host "   Current time: $(Get-Date -Format 'HH:mm:ss')" -ForegroundColor White
Write-Host ""

# Current timing
$currentUnixTime = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
$currentTimeLeft = $status.timeLeft
$currentEndTime = $currentUnixTime + $currentTimeLeft

Write-Host "⏰ CURRENT TIMING (WRONG - due to accident):" -ForegroundColor Red
Write-Host "   Time left: $([math]::Round($currentTimeLeft / 3600, 1)) hours" -ForegroundColor White
Write-Host "   Would finish at: $(Get-Date -UnixTimeSeconds $currentEndTime -Format 'HH:mm:ss')" -ForegroundColor White
Write-Host ""

# Calculate what it should be for 21:00 finish
$targetFinishTime = Get-Date -Hour 21 -Minute 0 -Second 0
$targetUnixTime = [DateTimeOffset]$targetFinishTime.ToUniversalTime().ToUnixTimeSeconds()
$correctTimeLeft = $targetUnixTime - $currentUnixTime

Write-Host "🎯 CORRECT TIMING (for 21:00 finish):" -ForegroundColor Green
Write-Host "   Target finish: 21:00:00" -ForegroundColor White
Write-Host "   Should have: $([math]::Round($correctTimeLeft / 3600, 1)) hours left" -ForegroundColor White
Write-Host "   Difference: $([math]::Round(($currentTimeLeft - $correctTimeLeft) / 3600, 1)) hours too much" -ForegroundColor White
Write-Host ""

# Calculate the adjustment needed
$stageAdjustment = $correctTimeLeft - $currentTimeLeft
$stageAdjustmentMinutes = [math]::Round($stageAdjustment / 60)

Write-Host "⚙️ HOW TO FIX:" -ForegroundColor Yellow
Write-Host ""

if ($stageAdjustmentMinutes -lt 0) {
    Write-Host "   Option 1: Manually advance when you have $([math]::Abs($stageAdjustmentMinutes)) minutes left" -ForegroundColor Cyan
    Write-Host "   Option 2: Use stage duration override to reduce time by $([math]::Abs($stageAdjustmentMinutes)) minutes" -ForegroundColor Cyan
} else {
    Write-Host "   Option 1: Use stage duration override to add $stageAdjustmentMinutes minutes" -ForegroundColor Cyan
    Write-Host "   Option 2: Wait for natural timing (you're actually ahead of schedule)" -ForegroundColor Cyan
}

Write-Host ""
Write-Host "🔧 STAGE DURATION OVERRIDE URL:" -ForegroundColor Green
Write-Host "   http://192.168.250.125/api/override_stage_duration?minutes=$([math]::Round($correctTimeLeft / 60))" -ForegroundColor White
Write-Host ""

Write-Host "📱 OR use the web interface:" -ForegroundColor Green
Write-Host "   1. Go to http://192.168.250.125/" -ForegroundColor White
Write-Host "   2. Look for stage duration controls" -ForegroundColor White
Write-Host "   3. Set remaining time to $([math]::Round($correctTimeLeft / 60)) minutes" -ForegroundColor White
Write-Host ""

# Show the fermentation adjustment impact
Write-Host "🧪 FERMENTATION IMPACT:" -ForegroundColor Blue
Write-Host "   Current fermentation factor: $($status.fermentationFactor)" -ForegroundColor White
Write-Host "   Temperature: $($status.temperature)°C" -ForegroundColor White
Write-Host "   This will affect actual timing vs scheduled timing" -ForegroundColor White
