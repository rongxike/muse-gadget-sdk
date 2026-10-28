<#
.SYNOPSIS
    Pack the Flappy Bird art and sounds into C arrays the firmware links directly.

.DESCRIPTION
    Reproduces, in one step, the manual asset conversion behind the Flappy Bird
    page (CONFIG_MUSE_FLAPPY). Everything is baked into the app image and read
    straight from flash (XIP, mapped through PSRAM on the 1.75C) - no runtime
    decode, no assets partition.

    Images (LVGL 9 lv_image_dsc_t, via managed_components/lvgl__lvgl/scripts/LVGLImage.py):
      flappy_bezel        466x466  RGB565A8  (transparent ring, drawn on top)
      flappy_pipe_top     72x370   RGB565A8
      flappy_pipe_bottom  72x370   RGB565A8
      flappy_bird_0/1/2   48x48    RGB565A8  (cropped from the 48x144 sheet)
      flappy_getready     scaled   RGB565_SWAPPED (opaque overlay)
      flappy_gameover     scaled   RGB565_SWAPPED (opaque overlay)

    The display runs LV_COLOR_FORMAT_RGB565_SWAPPED (muse_lcd_bands.c): opaque
    full-frame art uses RGB565_SWAPPED for a zero-conversion blit; sprites that
    need transparency use RGB565A8 (LVGL converts/blends them at draw time, the
    same path the avatar's RGB565 canvas already uses).

    Sounds (16 kHz mono signed-16 PCM, via the miniaudio package):
      flappy_sfx_flap   <- flap-wings.mp3
      flappy_sfx_over   <- game-over.mp3
      flappy_sfx_ready  <- game-ready.mp3
    all emitted into flappy_sfx.c and played by muse_flappy_ui.c's SFX task.

    Note on memory: the 1.75C now uses partitions_muse_waveshare_s3_175c.csv with
    6 MB app slots, so the Get Ready / Game Over overlays are packed at full
    resolution by default (-OverlayScale 1.0). Lower it only if you retarget a
    board with a smaller app slot.

    The background (flappy_bg.png) is intentionally NOT packed: the game draws a
    plain black tile, which matches the near-black art for free.

.PARAMETER SourceDir
    Folder with the source PNG/MP3 assets. Default: ..\assets\flappy-bird.

.PARAMETER OutDir
    Where the generated .c files are written. Default: ..\esp32\components\muse.

.PARAMETER OverlayScale
    Scale factor (0.1..1.0) for the Get Ready / Game Over overlays. 1.0 keeps
    their native 466x373; smaller shrinks them to save flash on tight slots.

.PARAMETER Python
    Python to use. Default: the ESP-IDF venv python, else `python` on PATH.

.PARAMETER SkipImages
    Skip the image conversion step.

.PARAMETER SkipSounds
    Skip the sound conversion step.

.EXAMPLE
    .\scripts\pack-assets.ps1
    Repack everything at full quality into esp32\components\muse.

.EXAMPLE
    .\scripts\pack-assets.ps1 -OverlayScale 0.72 -SkipSounds
    Repack only the images, shrinking the overlays (for a 4 MB app slot).
#>
[CmdletBinding()]
param(
    [string]$SourceDir,
    [string]$OutDir,
    [ValidateRange(0.1, 1.0)][double]$OverlayScale = 1.0,
    [string]$Python,
    [switch]$SkipImages,
    [switch]$SkipSounds
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $SourceDir) { $SourceDir = Join-Path $repoRoot "assets" }
if (-not $OutDir) { $OutDir = Join-Path $repoRoot "esp32\components\muse" }

$lvglConv = Join-Path $repoRoot "esp32\managed_components\lvgl__lvgl\scripts\LVGLImage.py"
$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("flappy_pack_" + [System.Guid]::NewGuid().ToString("N").Substring(0, 8))

# ---- asset manifest (edit here to add/rename assets) --------------------------

# Opaque, full-frame images -> RGB565_SWAPPED. The clock face is a baked
# static background; get-ready/game-over overlays are optionally scaled.
$opaque_full = @(
    @{ name = "clock_face"; file = "clock_face.png" },
    @{ name = "flappy_getready"; file = "flappy_getready.png" },
    @{ name = "flappy_gameover"; file = "flappy_gameover.png" }
)
# Sprites that need transparency -> RGB565A8, native size.
$sprites = @(
    @{ name = "flappy_bezel"; file = "flappy_bezel.png" },
    @{ name = "flappy_pipe_top"; file = "flappy_pipe_top.png" },
    @{ name = "flappy_pipe_bottom"; file = "flappy_pipe_bottom.png" }
)
# Bird sprite sheet (48x144) -> three 48x48 RGB565A8 frames.
$birdSheet = @{ file = "flappy_bird.png"; frame = 48; count = 3; prefix = "flappy_bird_" }
# Sounds -> 16 kHz mono PCM, all collected into flappy_sfx.c.
$sounds = @(
    @{ name = "flappy_sfx_flap"; file = "flap-wings.mp3" },
    @{ name = "flappy_sfx_over"; file = "game-over.mp3" },
    @{ name = "flappy_sfx_ready"; file = "game-ready.mp3" }
)

# ---- helpers -----------------------------------------------------------------

function Resolve-PythonExe {
    param([string]$Preferred)
    if ($Preferred) {
        if (Test-Path $Preferred) { return (Resolve-Path $Preferred).Path }
        throw "Python not found at -Python path: $Preferred"
    }
    $idfPy = Join-Path $HOME ".espressif\python_env\idf6.0_py3.11_env\Scripts\python.exe"
    if (Test-Path $idfPy) { return $idfPy }
    $cmd = Get-Command python -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    throw "No Python found. Pass -Python <path to python.exe>."
}

function Assert-File {
    param([string]$Path, [string]$What)
    if (-not (Test-Path $Path)) { throw "$What not found: $Path" }
}

function Ensure-PyModule {
    param([string]$Py, [string]$Import, [string]$Package)
    & $Py -c "import $Import" 2>$null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  installing $Package ..."
        & $Py -m pip install --quiet --disable-pip-version-check $Package
        if ($LASTEXITCODE -ne 0) { throw "Failed to install $Package" }
    }
}

function Invoke-Py {
    param([string]$Py, [string]$Code, [string[]]$PyArgs = @())
    $f = Join-Path $tmp ("step_" + [System.Guid]::NewGuid().ToString("N").Substring(0, 8) + ".py")
    Set-Content -Path $f -Value $Code -Encoding UTF8
    & $Py $f @PyArgs
    if ($LASTEXITCODE -ne 0) { throw "python step failed (exit $LASTEXITCODE)" }
}

# LVGLImage.py emits an #if/#elif include guard that falls back to
# "lvgl/lvgl.h", which this project's include paths don't provide. Rewrite it to
# the plain "lvgl.h" this repo uses (see clock_face.c).
function Repair-LvglInclude {
    param([string]$CFile)
    $raw = Get-Content $CFile -Raw
    if ($raw -match '#include "lvgl/lvgl\.h"') {
        $pattern = '#if defined\(LV_LVGL_H_INCLUDE_SIMPLE\)[\s\S]*?#include "lvgl/lvgl\.h"\s*#endif'
        $raw = [regex]::Replace($raw, $pattern, '#include "lvgl.h"')
        Set-Content -Path $CFile -Value $raw -NoNewline -Encoding UTF8
    }
}

function Convert-Image {
    param([string]$Py, [string]$Src, [string]$Name, [string]$Cf)
    & $Py $lvglConv --ofmt C --cf $Cf --name $Name -o $OutDir $Src | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "LVGLImage.py failed for $Name" }
    Repair-LvglInclude (Join-Path $OutDir "$Name.c")
}

function Show-Size {
    param([string]$Path)
    $kb = [math]::Round((Get-Item $Path).Length / 1KB, 1)
    "{0,9:N1} KB  {1}" -f $kb, (Split-Path $Path -Leaf)
}

# ---- run ---------------------------------------------------------------------

$Python = Resolve-PythonExe -Preferred $Python
Assert-File $SourceDir "Source asset folder"
Assert-File $OutDir "Output component folder"
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

Write-Host "Flappy asset packer"
Write-Host "  python : $Python"
Write-Host "  source : $SourceDir"
Write-Host "  output : $OutDir"
Write-Host "  overlay scale: $OverlayScale"
Write-Host ""

try {
    if (-not $SkipImages) {
        Write-Host "[images] checking Pillow ..."
        Ensure-PyModule -Py $Python -Import "PIL" -Package "Pillow"
        Assert-File $lvglConv "LVGLImage.py"

        Write-Host "[images] transparent sprites -> RGB565A8"
        foreach ($s in $sprites) {
            $src = Join-Path $SourceDir $s.file
            Assert-File $src "sprite $($s.file)"
            Convert-Image -Py $Python -Src $src -Name $s.name -Cf "RGB565A8"
            Write-Host ("  " + (Show-Size (Join-Path $OutDir "$($s.name).c")))
        }

        Write-Host "[images] bird sheet -> three $($birdSheet.frame)x$($birdSheet.frame) frames (RGB565A8)"
        $birdSrc = Join-Path $SourceDir $birdSheet.file
        Assert-File $birdSrc "bird sheet $($birdSheet.file)"
        $cropDir = Join-Path $tmp "bird"
        New-Item -ItemType Directory -Force -Path $cropDir | Out-Null
        $cropCode = @"
from PIL import Image
import sys
src, outdir, frame, count = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
im = Image.open(src).convert('RGBA')
for i in range(count):
    im.crop((0, i*frame, frame, i*frame+frame)).save(f'{outdir}/frame_{i}.png')
"@
        Invoke-Py -Py $Python -Code $cropCode -PyArgs @($birdSrc, $cropDir, "$($birdSheet.frame)", "$($birdSheet.count)")
        for ($i = 0; $i -lt $birdSheet.count; $i++) {
            $name = "$($birdSheet.prefix)$i"
            Convert-Image -Py $Python -Src (Join-Path $cropDir "frame_$i.png") -Name $name -Cf "RGB565A8"
            Write-Host ("  " + (Show-Size (Join-Path $OutDir "$name.c")))
        }

        Write-Host "[images] opaque full-frame images -> RGB565_SWAPPED (clock face + overlays)"
        $ovDir = Join-Path $tmp "overlay"
        New-Item -ItemType Directory -Force -Path $ovDir | Out-Null
        $resizeCode = @"
from PIL import Image
import sys
src, dst, scale = sys.argv[1], sys.argv[2], float(sys.argv[3])
im = Image.open(src).convert('RGB')
if scale != 1.0:
    w, h = im.size
    im = im.resize((max(1, round(w*scale)), max(1, round(h*scale))), Image.LANCZOS)
im.save(dst)
"@
        foreach ($o in $opaque_full) {
            $src = Join-Path $SourceDir $o.file
            Assert-File $src "image $($o.file)"
            $resized = Join-Path $ovDir "$($o.name).png"
            if ($o.name -eq "clock_face") {
                Copy-Item $src $resized -Force
            } else {
                Invoke-Py -Py $Python -Code $resizeCode -PyArgs @($src, $resized, "$OverlayScale")
            }
            Convert-Image -Py $Python -Src $resized -Name $o.name -Cf "RGB565_SWAPPED"
            Write-Host ("  " + (Show-Size (Join-Path $OutDir "$($o.name).c")))
        }
    }

    if (-not $SkipSounds) {
        Write-Host "[sounds] checking miniaudio ..."
        Ensure-PyModule -Py $Python -Import "miniaudio" -Package "miniaudio"

        Write-Host "[sounds] mp3 -> 16 kHz mono PCM -> flappy_sfx.c"
        $sfxOut = Join-Path $OutDir "flappy_sfx.c"
        # Build "name=path" pairs for the generator.
        $pairs = @()
        foreach ($s in $sounds) {
            $src = Join-Path $SourceDir $s.file
            Assert-File $src "sound $($s.file)"
            $pairs += ($s.name + "=" + $src)
        }
        $sfxCode = @"
import miniaudio, sys
out = sys.argv[1]
pairs = [a.split('=', 1) for a in sys.argv[2:]]
with open(out, 'w', newline='\n') as fo:
    fo.write('/* Generated by scripts/pack-assets.ps1 from the flappy-bird mp3 assets: */\n')
    fo.write('/* 16 kHz mono signed-16 PCM. Do not edit by hand. */\n')
    fo.write('#include <stdint.h>\n#include <stddef.h>\n\n')
    for name, path in pairs:
        dec = miniaudio.decode_file(path, output_format=miniaudio.SampleFormat.SIGNED16,
                                    nchannels=1, sample_rate=16000)
        s = dec.samples
        fo.write(f'const int16_t {name}[] = {{\n')
        for i in range(0, len(s), 16):
            fo.write('    ' + ','.join(str(v) for v in s[i:i+16]) + ',\n')
        fo.write('};\n')
        fo.write(f'const size_t {name}_len = {len(s)};\n\n')
"@
        Invoke-Py -Py $Python -Code $sfxCode -PyArgs (@($sfxOut) + $pairs)
        Write-Host ("  " + (Show-Size $sfxOut))
    }

    Write-Host ""
    Write-Host "Done. Rebuild with: .\scripts\deploy-routellm.ps1 -Port COM4"
}
finally {
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
