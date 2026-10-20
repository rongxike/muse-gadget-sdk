param(
    [string]$Port,
    [switch]$BuildOnly,
    [switch]$Reconfigure
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$esp32Dir = Join-Path $repoRoot "esp32"

$idfPyEnvScripts = Join-Path $HOME ".espressif\python_env\idf6.0_py3.11_env\Scripts"
$pythonExe = Join-Path $idfPyEnvScripts "python.exe"
$idfRoot = Join-Path $HOME "esp\esp-idf-v6.0.1"
$exportPs1 = Join-Path $idfRoot "export.ps1"
$idfPy = Join-Path $idfRoot "tools\idf.py"

$buildArgs = @(
    "-B", "build-muse-routellm",
    "-DIDF_TARGET=esp32s3",
    "-DSDKCONFIG=build-muse-routellm/sdkconfig",
    "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;devices/sdkconfig.muse;devices/sdkconfig.muse-waveshare-s3-175c;sdkconfig.routellm"
)

function Assert-PathExists {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Name
    )

    if (-not (Test-Path $Path)) {
        throw "$Name not found: $Path"
    }
}

function Get-Esp32ComPort {
    $ports = @(Get-CimInstance Win32_PnPEntity |
        Where-Object { $_.PNPDeviceID -match "VID_303A&PID_1001" } |
        ForEach-Object {
            if ($_.Name -match "\((COM\d+)\)") {
                $Matches[1]
            }
        } |
        Where-Object { $_ } |
        Sort-Object -Unique)

    if (-not $ports) {
        return $null
    }

    return $ports[0]
}

Assert-PathExists -Path $esp32Dir -Name "esp32 project directory"
Assert-PathExists -Path $pythonExe -Name "ESP-IDF Python"
Assert-PathExists -Path $exportPs1 -Name "ESP-IDF export.ps1"
Assert-PathExists -Path $idfPy -Name "idf.py"

if (-not $Port) {
    $Port = Get-Esp32ComPort
    if (-not $Port) {
        throw "No ESP32 serial port found (VID:PID 303A:1001). Pass -Port COMx."
    }
}

Write-Host "Using port: $Port"
Write-Host "Project: $esp32Dir"

Push-Location $esp32Dir
try {
    $env:PATH = "$idfPyEnvScripts;$env:PATH"

    # ESP-IDF's export.ps1 can emit native stderr/status text that PowerShell
    # may surface as NativeCommandError when ErrorActionPreference is Stop.
    # Run export in a tolerant block, then restore the caller's preference.
    $oldEap = $ErrorActionPreference
    $oldNativePref = $null
    $hasNativePref = $false
    try {
        $ErrorActionPreference = "Continue"
        if (Get-Variable -Name PSNativeCommandUseErrorActionPreference -Scope Global -ErrorAction SilentlyContinue) {
            $hasNativePref = $true
            $oldNativePref = $Global:PSNativeCommandUseErrorActionPreference
            $Global:PSNativeCommandUseErrorActionPreference = $false
        }
        . $exportPs1 2>$null
    }
    finally {
        $ErrorActionPreference = $oldEap
        if ($hasNativePref) {
            $Global:PSNativeCommandUseErrorActionPreference = $oldNativePref
        }
    }

    if ($Reconfigure) {
        Remove-Item -Force "build-muse-routellm\sdkconfig" -ErrorAction SilentlyContinue
    }

    Write-Host "[1/2] Building build-muse-routellm..."
    & $pythonExe $idfPy @buildArgs build
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed with exit code $LASTEXITCODE"
    }

    if ($BuildOnly) {
        Write-Host "Build complete. Skipping flash because -BuildOnly was used."
        exit 0
    }

    Write-Host "[2/2] Flashing to $Port..."
    & $pythonExe $idfPy @buildArgs -p $Port flash
    if ($LASTEXITCODE -ne 0) {
        throw "Flash failed with exit code $LASTEXITCODE"
    }

    Write-Host "Done: build + flash completed successfully."
}
finally {
    Pop-Location
}
