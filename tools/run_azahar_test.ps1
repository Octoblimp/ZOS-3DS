# Boots ZOS-3DS.3dsx in Azahar, captures the window, presses START
# to leave the main loop, and reports a verdict from sdmc:/zos_debug.txt.
#
# What it took to get right:
#
#   * Pass the .3dsx, never the .cia: Azahar cannot boot a CIA from a path.
#   * Capture with PrintWindow(hWnd, hdc, PW_RENDERFULLCONTENT); a BitBlt of
#     the GPU-composited window comes back black.
#   * SetProcessDPIAware() first, or a scaled display crops the capture to
#     the top-left and loses the bottom screen.
#   * Keys go in with PostMessage, never SendKeys, so Azahar is never pulled
#     into the foreground. Azahar's default keyboard profile: A='A', B='S',
#     X='Z', Y='X', START='M', SELECT='N', D-pad = T/G/F/H, HOME='B'.
#
# The game data must be on Azahar's card first:
#   python tools/prepare_data.py --azahar
#
# Usage:
#   powershell -File tools/run_azahar_test.ps1
#   powershell -File tools/run_azahar_test.ps1 -HoldSeconds 10 -OutDir C:\shots
#   powershell -File tools/run_azahar_test.ps1 -Steps "tap:20,120;wait:800;shot:menu;key:B"
#
# -Steps runs after the hold, before START: ';'-separated
#   tap:x,y              the stylus at bottom-screen pixel (x, y), from its top left
#   drag:x1,y1,x2,y2     the stylus pressed at the first, moved, lifted at the second
#   key:NAME             A B X Y L R START SELECT UP DOWN LEFT RIGHT HOME
#   hold:NAME,ms         the key held down for ms
#   wait:ms              (each tap, drag and key already waits 400 ms after)
#   shot:name            a screenshot
# Taps are mouse messages posted to the render window, mapped through the
# default layout (top screen over bottom, centred, scaled to fit).

param(
    [int]$HoldSeconds = 4,        # how long to watch the main loop before START
    [int]$SelectPresses = 0,      # then press SELECT this many times, a shot after each
    [int]$Shots = 1,              # shots spread over the hold (the last at its end)
    [string]$AzaharExe = "C:\Program Files\Azahar\azahar.exe",
    [string]$Target3dsx = "",
    [string]$Steps = "",          # see the usage above
    [string]$WaitFor = "entering main loop",  # the log line the hold starts from
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
if ($Target3dsx -eq "") { $Target3dsx = Join-Path $root "3DS Files\ZOS-3DS.3dsx" }
if ($OutDir -eq "") { $OutDir = Join-Path $env:TEMP "zos_azahar" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

if (-not (Test-Path $AzaharExe)) { throw "Azahar not found at $AzaharExe" }
if (-not (Test-Path $Target3dsx)) { throw "Target .3dsx not found at $Target3dsx" }
if ($Target3dsx -like "*.cia") {
    throw "Refusing to run a .cia: Azahar cannot boot one from a path. Pass the .3dsx."
}

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Drawing;
public class ZosCap {
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    public delegate bool EnumProc(IntPtr h, IntPtr p);
    // Azahar's "Update Available" dialog takes the foreground on launch and
    // keeps the emulator from acting on posted keys. Anything the process owns
    // that is visible, Qt, titled, and not titled "Azahar" is one of those.
    static uint sPid; static IntPtr sKeep; static int sClosed;
    public static int DismissPopups(uint pid, IntPtr keep) {
        sPid = pid; sKeep = keep; sClosed = 0;
        EnumWindows(delegate(IntPtr h, IntPtr p) {
            uint q; GetWindowThreadProcessId(h, out q);
            if (q != sPid || h == sKeep || !IsWindowVisible(h)) { return true; }
            StringBuilder c = new StringBuilder(256); GetClassName(h, c, 256);
            if (!c.ToString().StartsWith("Qt")) { return true; }
            StringBuilder t = new StringBuilder(256); GetWindowText(h, t, 256);
            string title = t.ToString();
            if (title.Length == 0 || title.StartsWith("Azahar")) { return true; }
            PostMessage(h, 0x0010, IntPtr.Zero, IntPtr.Zero);   // WM_CLOSE
            sClosed++;
            return true;
        }, IntPtr.Zero);
        return sClosed;
    }
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern int PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern IntPtr PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    public struct RECT { public int Left, Top, Right, Bottom; }
    public static void PressKey(IntPtr h, int vk) {
        PostMessage(h, 0x0100, (IntPtr)vk, IntPtr.Zero);    // WM_KEYDOWN
        // A shorter hold can land inside one emulated frame, unseen by hidScanInput.
        System.Threading.Thread.Sleep(140);
        PostMessage(h, 0x0101, (IntPtr)vk, IntPtr.Zero);    // WM_KEYUP
    }
    public static void HoldKey(IntPtr h, int vk, int ms) {
        PostMessage(h, 0x0100, (IntPtr)vk, IntPtr.Zero);
        System.Threading.Thread.Sleep(ms);
        PostMessage(h, 0x0101, (IntPtr)vk, IntPtr.Zero);
    }
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc cb, IntPtr p);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    public struct POINT { public int X, Y; }
    // The render surface: the largest visible child window, or the window.
    static IntPtr sBest; static long sBestArea;
    public static IntPtr RenderWindow(IntPtr top) {
        sBest = IntPtr.Zero; sBestArea = 0;
        EnumChildWindows(top, delegate(IntPtr h, IntPtr p) {
            if (!IsWindowVisible(h)) { return true; }
            RECT r; GetClientRect(h, out r);
            long a = (long)(r.Right - r.Left) * (r.Bottom - r.Top);
            if (a > sBestArea) { sBestArea = a; sBest = h; }
            return true;
        }, IntPtr.Zero);
        return sBest != IntPtr.Zero ? sBest : top;
    }
    public static string Describe(IntPtr h) {
        RECT r; GetClientRect(h, out r);
        POINT o = new POINT(); ClientToScreen(h, ref o);
        StringBuilder c = new StringBuilder(256); GetClassName(h, c, 256);
        return String.Format("{0} {1}x{2} at {3},{4}", c, r.Right, r.Bottom, o.X, o.Y);
    }
    // Bottom-screen pixel -> the render window's client point, in the
    // default layout: 400x480 (top over bottom) scaled to fit and centred.
    public static POINT BottomToClient(IntPtr h, double bx, double by) {
        RECT r; GetClientRect(h, out r);
        double w = r.Right, ht = r.Bottom;
        double s = Math.Min(w / 400.0, ht / 480.0);
        double ox = (w - 320.0 * s) / 2.0, oy = (ht - 480.0 * s) / 2.0 + 240.0 * s;
        POINT p = new POINT(); p.X = (int)(ox + (bx + 0.5) * s); p.Y = (int)(oy + (by + 0.5) * s);
        return p;
    }
    static IntPtr Lp(POINT p) { return (IntPtr)((p.Y << 16) | (p.X & 0xFFFF)); }
    public static void Mouse(IntPtr h, uint msg, POINT p, int buttons) {
        PostMessage(h, msg, (IntPtr)buttons, Lp(p));
    }
    public static void CaptureToFile(IntPtr h, string path) {
        RECT r; GetWindowRect(h, out r);
        int w = r.Right - r.Left, ht = r.Bottom - r.Top;
        using (Bitmap bmp = new Bitmap(w, ht))
        using (Graphics g = Graphics.FromImage(bmp)) {
            IntPtr hdc = g.GetHdc();
            PrintWindow(h, hdc, 2);   // PW_RENDERFULLCONTENT
            g.ReleaseHdc(hdc);
            bmp.Save(path, System.Drawing.Imaging.ImageFormat.Png);
        }
    }
}
"@ -ReferencedAssemblies System.Drawing -ErrorAction SilentlyContinue

[void][ZosCap]::SetProcessDPIAware()

$VK_M = 77   # START
$VK_N = 78   # SELECT
# Azahar's default keyboard profile; the circle pad is the arrow keys.
$KeyCodes = @{ A = 65; B = 83; X = 90; Y = 88; L = 81; R = 87; START = 77; SELECT = 78;
               UP = 84; DOWN = 71; LEFT = 70; RIGHT = 72; HOME = 66 }

$debugLog  = Join-Path $env:APPDATA "Azahar\sdmc\zos_debug.txt"
$azaharLog = Join-Path $env:APPDATA "Azahar\log\azahar_log.txt"
Remove-Item $debugLog  -ErrorAction SilentlyContinue
Remove-Item $azaharLog -ErrorAction SilentlyContinue
Get-ChildItem $OutDir -Filter *.png -ErrorAction SilentlyContinue | Remove-Item -Force

Write-Host "Launching Azahar with $Target3dsx"
$proc = Start-Process -FilePath $AzaharExe -ArgumentList "`"$Target3dsx`"" -PassThru

$deadline = (Get-Date).AddSeconds(60)
while ((Get-Date) -lt $deadline) {
    $proc.Refresh()
    if ($proc.HasExited) { throw "Azahar exited before opening a window" }
    if ($proc.MainWindowHandle -ne 0) { break }
    Start-Sleep -Milliseconds 250
}
$hwnd = $proc.MainWindowHandle
if ($hwnd -eq 0) { $proc | Stop-Process -Force; throw "no Azahar window" }

$start = Get-Date
function Elapsed { ((Get-Date) - $start).TotalSeconds }
function Alive { $proc.Refresh(); -not $proc.HasExited }

function Wait-ForLog([string]$pattern, [int]$timeoutSec) {
    $end = (Get-Date).AddSeconds($timeoutSec)
    while ((Get-Date) -lt $end) {
        if (-not (Alive)) { return $false }
        [void][ZosCap]::DismissPopups([uint32]$proc.Id, $hwnd)
        if (Test-Path $debugLog) {
            if (Select-String -Path $debugLog -Pattern $pattern -Quiet -ErrorAction SilentlyContinue) { return $true }
        }
        Start-Sleep -Milliseconds 300
    }
    return $false
}

$step = 0
function Shot([string]$tag) {
    if (-not (Alive)) { return }
    $script:step++
    $path = Join-Path $OutDir ("{0:d2}_{1}.png" -f $script:step, $tag)
    [ZosCap]::CaptureToFile($hwnd, $path)
    Write-Host ("  [{0,5:N1}s] {1}" -f (Elapsed), $path)
}

$booted = Wait-ForLog $WaitFor 60
Write-Host ("  main loop: {0}" -f $booted)
if ($booted) {
    if ($Shots -le 1) {
        Start-Sleep -Seconds $HoldSeconds
        Shot "main"
    } else {
        $gap = [int]($HoldSeconds * 1000 / $Shots)
        for ($i = 1; $i -le $Shots; $i++) {
            Start-Sleep -Milliseconds $gap
            Shot ("hold{0}" -f $i)
        }
    }
    if ($Steps -ne "") {
        $render = [ZosCap]::RenderWindow($hwnd)
        Write-Host ("  render window: {0}" -f [ZosCap]::Describe($render))
        foreach ($s in ($Steps -split ";")) {
            $s = $s.Trim(); if ($s -eq "") { continue }
            $kind, $arg = $s -split ":", 2
            switch ($kind) {
                "tap" {
                    $x, $y = $arg -split "," | ForEach-Object { [double]$_ }
                    $p = [ZosCap]::BottomToClient($render, $x, $y)
                    [ZosCap]::Mouse($render, 0x0200, $p, 0)      # WM_MOUSEMOVE
                    [ZosCap]::Mouse($render, 0x0201, $p, 1)      # WM_LBUTTONDOWN
                    Start-Sleep -Milliseconds 120
                    [ZosCap]::Mouse($render, 0x0202, $p, 0)      # WM_LBUTTONUP
                    Start-Sleep -Milliseconds 400
                }
                "drag" {
                    $v = $arg -split "," | ForEach-Object { [double]$_ }
                    $n = 12
                    $p = [ZosCap]::BottomToClient($render, $v[0], $v[1])
                    [ZosCap]::Mouse($render, 0x0200, $p, 0)
                    [ZosCap]::Mouse($render, 0x0201, $p, 1)
                    Start-Sleep -Milliseconds 60
                    for ($k = 1; $k -le $n; $k++) {
                        $p = [ZosCap]::BottomToClient($render, $v[0] + ($v[2] - $v[0]) * $k / $n,
                                                      $v[1] + ($v[3] - $v[1]) * $k / $n)
                        [ZosCap]::Mouse($render, 0x0200, $p, 1)
                        Start-Sleep -Milliseconds 33
                    }
                    [ZosCap]::Mouse($render, 0x0202, $p, 0)
                    Start-Sleep -Milliseconds 400
                }
                "key" {
                    [ZosCap]::PressKey($hwnd, $KeyCodes[$arg.ToUpper()])
                    Start-Sleep -Milliseconds 400
                }
                "hold" {
                    $v = $arg -split ","
                    [ZosCap]::HoldKey($hwnd, $KeyCodes[$v[0].ToUpper()], [int]$v[1])
                    Start-Sleep -Milliseconds 400
                }
                "wait" { Start-Sleep -Milliseconds ([int]$arg) }
                "shot" { Shot $arg }
                default { Write-Host "  unknown step $s" }
            }
        }
    }
    for ($i = 1; $i -le $SelectPresses; $i++) {
        [ZosCap]::PressKey($hwnd, $VK_N)
        Start-Sleep -Milliseconds 1500
        Shot ("select{0}" -f $i)
    }
    [ZosCap]::PressKey($hwnd, $VK_M)          # START -> leave the main loop
    $left = Wait-ForLog "^left main loop" 10
    Write-Host ("  left main loop: {0}" -f $left)
}

Start-Sleep -Milliseconds 800
$survived = Alive
if ($survived) { $proc | Stop-Process -Force }
Start-Sleep -Milliseconds 600

# --- the verdict ------------------------------------------------------------

$dbg = @(); if (Test-Path $debugLog)  { $dbg = Get-Content $debugLog }
$az  = @(); if (Test-Path $azaharLog) { $az  = Get-Content $azaharLog }

$crashSigs = @("PANIC", "segfault", "Unhandled exception", "svcBreak")
$crash = $az | Where-Object { $l = $_; $crashSigs | Where-Object { $l -match $_ } }

$reached  = $dbg | Where-Object { $_ -match "entering main loop" }
$leftLoop = $dbg | Where-Object { $_ -match "^left main loop" }
$logo     = $dbg | Where-Object { $_ -match "^logo " }

$verdict = "UNKNOWN"
if (-not $reached)       { $verdict = "FAIL (never reached the main loop)" }
elseif ($crash)          { $verdict = "FAIL (crash: $($crash[0]))" }
elseif (-not $leftLoop)  { $verdict = "FAIL (START did not leave the main loop)" }
else                     { $verdict = "PASS (booted, ran, left the main loop on START)" }

Write-Host ""
Write-Host "=== zos_debug.txt ==="
$dbg | Select-Object -Last 30 | ForEach-Object { Write-Host $_ }
Write-Host ""
Write-Host "=== azahar_log.txt errors ==="
$az | Where-Object { $_ -match "Error|Critical|PANIC" } | Select-Object -Last 12 | ForEach-Object { Write-Host $_ }
Write-Host ""
Write-Host "=== Result ==="
Write-Host ("Verdict:     {0}" -f $verdict)
if ($logo) { Write-Host ("Logo:        {0}" -f ($logo | Select-Object -First 1)) }
Write-Host ("Screenshots: {0} in {1}" -f $step, $OutDir)
