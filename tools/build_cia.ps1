# Builds 3DS Files/ZOS-3DS.cia (.elf/.3dsx/.smdh too).
#
# Why this script exists: the project's path contains spaces ("3DS Files",
# and often the folders above it), and devkitPro's Makefile template can't be
# invoked from such a path -- GNU Make's $(notdir ...) is whitespace-list
# based, so $(notdir $(CURDIR)) returns garbage and the top-level/recursive
# build split in the Makefile breaks before any compiler even runs. The
# workaround is to `subst` a temporary space-free drive letter onto
# "3DS Files" and build from there. This script automates that: it maps the
# drive, runs `make cia` inside devkitPro's own msys2 bash (which has
# DEVKITPRO/DEVKITARM and PATH already set correctly), and always unmaps the
# drive afterwards even if the build fails.
#
# Usage: powershell -ExecutionPolicy Bypass -File tools/build_cia.ps1
#        ... -DriveLetter Y:   to force one particular mapping
#        ... -NoData           to leave the game's data out (see below)
#
# The game's data: by default the .3dsx and the .cia carry it in their RomFS,
# from out/ZombieOutbreakSim (python tools/prepare_data.py makes it from the
# APK), through a junction "3DS Files/romfs" -- a path outside the subst'd
# folder cannot be reached from the build drive. That build holds Binary
# Space's art and sound: it is for your own console, not for handing
# out. -NoData builds the engine alone, which reads the data from
# sdmc:/ZombieOutbreakSim/ instead.

param(
    # Left empty, the first free letter out of the candidate list below is
    # taken. A letter is only used if `subst` does not already own it: another
    # project may keep its own build drive mapped, and clobbering that would
    # break whatever is building there.
    [string]$DriveLetter = "",
    [switch]$NoData
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$projectDir = Join-Path $root "3DS Files"
$toolsBin = Join-Path $root "tools\3ds-bin"

if (-not (Test-Path $projectDir)) {
    throw "Project directory not found: $projectDir"
}

# makerom and bannertool are not in devkitPro's packages: their Windows
# builds go in tools\3ds-bin.
foreach ($exe in @("makerom.exe", "bannertool.exe")) {
    if (-not (Test-Path (Join-Path $toolsBin $exe))) {
        throw "tools\3ds-bin\$exe is missing -- the README's Requirements say where to get it."
    }
}

# The HOME Menu's icon, banner and banner sound are made from the APK's art
# and sound (tools/make_meta.py, tools/banner_sound.py), when they are not
# there yet.
$meta = Join-Path $projectDir "meta"
if (-not ((Test-Path (Join-Path $meta "icon.png")) -and (Test-Path (Join-Path $meta "banner.png")))) {
    & python (Join-Path $PSScriptRoot "make_meta.py")
    if ($LASTEXITCODE -ne 0) { throw "make_meta.py failed" }
}
if (-not (Test-Path (Join-Path $meta "banner_sound.wav"))) {
    & python (Join-Path $PSScriptRoot "banner_sound.py")
    if ($LASTEXITCODE -ne 0) { throw "banner_sound.py failed (run python tools/prepare_data.py first)" }
}

# Every letter `subst` currently owns, as "Z:" strings. `subst` with no
# arguments prints one line per mapping, of the form "Z:\: => C:\some\path".
$substTaken = @()
foreach ($line in (& subst.exe)) {
    if ($line -match '^([A-Za-z]):') { $substTaken += ($matches[1].ToUpper() + ":") }
}

function Test-DriveFree($letter) {
    if ($substTaken -contains $letter) { return $false }
    # A real volume or a mapped network drive on that letter would be shadowed
    # by the subst, so those are out too.
    return -not (Test-Path ($letter + "\"))
}

if ($DriveLetter -ne "") {
    if ($DriveLetter -notmatch '^[A-Za-z]:$') {
        throw "-DriveLetter must look like 'Y:' -- got '$DriveLetter'"
    }
    $driveLetter = $DriveLetter.ToUpper()
    if (-not (Test-DriveFree $driveLetter)) {
        throw "$driveLetter is already in use (run 'subst $driveLetter /D' if it is a stale mapping) -- aborting so we don't clobber it."
    }
} else {
    $driveLetter = $null
    foreach ($cand in @("Z:", "Y:", "X:", "W:", "V:", "U:", "T:", "S:", "R:", "Q:")) {
        if (Test-DriveFree $cand) { $driveLetter = $cand; break }
    }
    if (-not $driveLetter) {
        throw "No free drive letter to map the build directory onto (tried Z: down to Q:)."
    }
}
Write-Host "Building from $driveLetter -> $projectDir"

$romfsLink = Join-Path $projectDir "romfs"
$dataDir = Join-Path $root "out\ZombieOutbreakSim"
$romfsArg = ""
if ($NoData) {
    Write-Host "No game data in this build (-NoData): it reads sdmc:/ZombieOutbreakSim/."
} else {
    if (-not (Test-Path (Join-Path $dataDir "images\GUI\MDPI\HomeScreen.t3x"))) {
        throw "No game data at $dataDir -- run 'python tools/prepare_data.py' first, or pass -NoData."
    }
    if (-not (Test-Path $romfsLink)) {
        New-Item -ItemType Junction -Path $romfsLink -Target $dataDir | Out-Null
    }
    $romfsArg = "ROMFS=romfs"
    Write-Host "Game data in the RomFS, from $dataDir"
}
# make remakes the .3dsx only when the .elf changes, and with or without the
# data it differs.
Remove-Item (Join-Path $projectDir "ZOS-3DS.3dsx") -ErrorAction SilentlyContinue

# devkitPro's generated .d files record every source path as seen from the
# build drive ("/z/source/car.v.pica"), so a build directory left behind by a
# run on a *different* letter makes make fail with "No rule to make target".
# The letter is therefore stamped next to the objects, and a change wipes them.
$buildDir = Join-Path $projectDir "build"
$stampPath = Join-Path $buildDir ".subst-drive"
if (Test-Path $buildDir) {
    $prev = if (Test-Path $stampPath) { (Get-Content $stampPath -Raw).Trim() } else { "" }
    if ($prev -ne $driveLetter) {
        Write-Host "Build drive changed ('$prev' -> '$driveLetter') -- clearing stale objects."
        Remove-Item $buildDir -Recurse -Force
    }
}
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
Set-Content -Path $stampPath -Value $driveLetter -Encoding ASCII

& subst.exe $driveLetter $projectDir
if ($LASTEXITCODE -ne 0) {
    throw "subst failed to map $driveLetter to $projectDir"
}

try {
    function ToMsysPath($winPath) {
        # C:\foo\bar -> /c/foo/bar (msys-style), since msys make doesn't
        # reliably resolve "C:/foo/bar"-style paths passed as variable values.
        $drive = $winPath.Substring(0, 1).ToLower()
        $rest = ($winPath.Substring(2)) -replace '\\', '/'
        return "/$drive$rest"
    }
    $makerom = ToMsysPath (Join-Path $toolsBin "makerom.exe")
    $bannertool = ToMsysPath (Join-Path $toolsBin "bannertool.exe")
    $bashExe = "C:\devkitPro\msys2\usr\bin\bash.exe"

    if (-not (Test-Path $bashExe)) {
        throw "devkitPro msys2 bash not found at $bashExe -- update this script if devkitPro is installed elsewhere."
    }

    # Written to a temp script instead of passed as a -lc string: PowerShell's
    # argument marshalling to native exes doesn't reliably preserve embedded
    # quotes/spaces, so a long quoted -lc string gets re-split on its way to
    # bash.exe.
    $scriptPath = Join-Path $env:TEMP "zos_build_cia.sh"
    # msys spells a drive-letter root "/z", lower case and without the colon.
    $msysDrive = "/" + $driveLetter.Substring(0, 1).ToLower()
    @"
cd $msysDrive
make cia MAKEROM="$makerom" BANNERTOOL="$bannertool" $romfsArg
"@ | Set-Content -Path $scriptPath -Encoding ASCII -NoNewline

    & $bashExe -l $scriptPath
    $buildExit = $LASTEXITCODE
    Remove-Item $scriptPath -ErrorAction SilentlyContinue
}
finally {
    & subst.exe $driveLetter /D
}

if ($buildExit -ne 0) {
    throw "make cia failed (exit $buildExit)"
}

$cia = Join-Path $projectDir "ZOS-3DS.cia"
if (Test-Path $cia) {
    Write-Host "Built: $cia"
} else {
    throw "make cia reported success but $cia is missing"
}

# A relocation the 3DSX loader refuses links cleanly and fails only when
# Azahar starts the program, with nothing in the app's own log; see
# tools/check_3dsx.py. It is checked here so the build says so first.
$threedsx = Join-Path $projectDir "ZOS-3DS.3dsx"
& python (Join-Path $PSScriptRoot "check_3dsx.py") $threedsx
if ($LASTEXITCODE -ne 0) {
    throw "check_3dsx: $threedsx would not load"
}
