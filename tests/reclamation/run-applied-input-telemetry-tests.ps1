$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$programFilesX86 = [Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFilesX86)
if (-not $programFilesX86) { $programFilesX86 = 'C:\Program Files (x86)' }
$vswhere = Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'Visual Studio Installer vswhere.exe was not found.' }

$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Visual Studio C++ Build Tools were not found.' }

$devCmd = Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat'
$outDir = Join-Path ([System.IO.Path]::GetTempPath()) 'reclamation-zero-applied-input-telemetry-tests'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$testSource = Join-Path $repoRoot 'tests\reclamation\AppliedInputTelemetryTests.cpp'
$testExe = Join-Path $outDir 'ReclamationAppliedInputTelemetryTests.exe'

$compile = '"{0}" -arch=x64 -host_arch=x64 >nul && cl /nologo /std:c++20 /EHsc /I"{1}" "{2}" /Fe:"{3}"' -f $devCmd, $repoRoot, $testSource, $testExe
Push-Location $outDir
cmd.exe /d /s /c $compile
$compileExit = $LASTEXITCODE
Pop-Location
if ($compileExit -ne 0) { throw "Applied-input telemetry test compilation failed with exit code $compileExit." }

& $testExe
if ($LASTEXITCODE -ne 0) { throw "Applied-input telemetry tests failed with exit code $LASTEXITCODE." }
