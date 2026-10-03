$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

$cppSuites = @(
    'run-policy-tests.ps1',
    'run-duel-policy-tests.ps1',
    'run-trajectory-predictor-tests.ps1',
    'run-applied-input-telemetry-tests.ps1'
)

foreach ($suite in $cppSuites) {
    & (Join-Path $PSScriptRoot $suite)
    if ($LASTEXITCODE -ne 0) { throw "$suite failed with exit code $LASTEXITCODE." }
}

Push-Location $repoRoot
try {
    python -m unittest discover -s 'tools\reclamation_duel_eval\tests' -p 'test_*.py'
    if ($LASTEXITCODE -ne 0) { throw "reclamation_duel_eval tests failed with exit code $LASTEXITCODE." }
} finally {
    Pop-Location
}

foreach ($tool in @('reclamation_replay_extract', 'reclamation_training_data')) {
    $toolRoot = Join-Path $repoRoot "tools\$tool"
    Push-Location $toolRoot
    try {
        python -m unittest discover -s . -p 'test_*.py'
        if ($LASTEXITCODE -ne 0) { throw "$tool tests failed with exit code $LASTEXITCODE." }
    } finally {
        Pop-Location
    }
}

Write-Host 'RECLAMATION BOT CORE FOCUSED TESTS: PASS'
