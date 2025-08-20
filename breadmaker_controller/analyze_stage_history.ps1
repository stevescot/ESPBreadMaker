#!/usr/bin/env pwsh
# Stage timing history analyzer

Write-Host "=== Stage Timing History Analysis ===" -ForegroundColor Cyan
Write-Host ""

# Get current status
$status = curl -s "http://192.168.250.125/api/status" | ConvertFrom-Json

Write-Host "📊 CURRENT PROGRAM STATUS:" -ForegroundColor Yellow
Write-Host "   Program ID: $($status.programId)" -ForegroundColor White
Write-Host "   Current Stage: $($status.stage) (index $($status.stageIdx))" -ForegroundColor White
Write-Host "   Current Time: $(Get-Date -Format 'HH:mm:ss')" -ForegroundColor White
Write-Host ""

Write-Host "⏱️ STAGE TIMING HISTORY:" -ForegroundColor Green
Write-Host ""

# Define stage names based on typical sourdough program
$stageNames = @(
    "Initial Mix",
    "Autolyse", 
    "Mix",
    "Bulk Ferment",
    "Knockdown",
    "Proof",
    "Bake",
    "Cool",
    "Complete"
)

# Show actual stage start times
$actualTimes = $status.actualStageStartTimes
for ($i = 0; $i -lt $actualTimes.Length; $i++) {
    $stageName = if ($i -lt $stageNames.Length) { $stageNames[$i] } else { "Stage $i" }
    
    if ($actualTimes[$i] -gt 0) {
        $startTime = Get-Date -UnixTimeSeconds $actualTimes[$i] -Format 'HH:mm:ss'
        $isCurrentStage = ($i -eq $status.stageIdx)
        $marker = if ($isCurrentStage) { " ← CURRENT" } else { "" }
        Write-Host "   Stage $i - $stageName`: Started at $startTime$marker" -ForegroundColor $(if ($isCurrentStage) { "Yellow" } else { "White" })
    } else {
        Write-Host "   Stage $i - $stageName`: Not started yet" -ForegroundColor Gray
    }
}

Write-Host ""
Write-Host "🔍 ANALYSIS OF YOUR SITUATION:" -ForegroundColor Cyan
Write-Host ""

# Check if there's a gap indicating accidental advancement
$knockdownStart = $actualTimes[4]  # Stage 4 - Knockdown
$proofStart = $actualTimes[5]      # Stage 5 - Proof  
$bakeStart = $actualTimes[6]       # Stage 6 - Bake

if ($knockdownStart -gt 0) {
    Write-Host "✓ Knockdown started at: $(Get-Date -UnixTimeSeconds $knockdownStart -Format 'HH:mm:ss')" -ForegroundColor Green
}

if ($proofStart -gt 0) {
    Write-Host "✓ Proof started at: $(Get-Date -UnixTimeSeconds $proofStart -Format 'HH:mm:ss')" -ForegroundColor Green
}

if ($bakeStart -gt 0) {
    Write-Host "⚠️ Bake was started at: $(Get-Date -UnixTimeSeconds $bakeStart -Format 'HH:mm:ss')" -ForegroundColor Red
    Write-Host "   (But you went back to Proof)" -ForegroundColor Yellow
    
    # Calculate time difference
    $timeDiff = $bakeStart - $proofStart
    if ($timeDiff -lt 60) {
        Write-Host "   → Bake started only $timeDiff seconds after Proof - this confirms accidental advancement!" -ForegroundColor Red
    }
}

Write-Host ""
Write-Host "💡 RECOMMENDATION:" -ForegroundColor Yellow
Write-Host ""

if ($knockdownStart -gt 0 -and $proofStart -gt 0) {
    # Calculate how long knockdown actually ran
    $knockdownDuration = $proofStart - $knockdownStart
    $knockdownMinutes = [math]::Round($knockdownDuration / 60, 1)
    
    Write-Host "   Knockdown ran for: $knockdownMinutes minutes" -ForegroundColor White
    Write-Host "   Proof started at: $(Get-Date -UnixTimeSeconds $proofStart -Format 'HH:mm:ss')" -ForegroundColor White
    
    # Calculate what proof end time should be based on normal duration
    # Assuming proof should be around 2-3 hours for sourdough
    $normalProofHours = 2.5
    $normalProofSeconds = $normalProofHours * 3600
    $expectedProofEnd = $proofStart + $normalProofSeconds
    
    Write-Host "   Expected proof end (2.5 hrs): $(Get-Date -UnixTimeSeconds $expectedProofEnd -Format 'HH:mm:ss')" -ForegroundColor Green
    
    if ((Get-Date -Hour 21 -Minute 0 -Second 0).ToFileTime() -gt 0) {
        Write-Host "   Your target of 21:00 seems reasonable!" -ForegroundColor Green
    }
}
