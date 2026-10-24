# End-to-end GUI probe for DPKViewer.exe. ASCII only (PS 5.1 reads non-BOM files as ANSI, so any
# non-ASCII byte would break parsing); every Chinese string below is read back from the app.
# All interactions are window messages targeted at specific handles - no global keyboard or mouse
# injection, so other applications on the desktop are never affected.
#
#   1) launch + screenshot the main window
#   2) usage-help button -> help dialog -> close
#   3) open-package button -> file dialog -> fill the file name -> OK -> screenshot the loaded package
#   4) extract button -> folder picker -> set output dir -> OK -> completion message box -> screenshot
#   5) pack button -> folder picker (source) -> OK -> save dialog (target) -> OK -> message box -> screenshot
#   6) close the app, report the exit code
param(
    [Parameter(Mandatory=$true)][string]$Exe,
    [Parameter(Mandatory=$true)][string]$Pkg,
    [Parameter(Mandatory=$true)][string]$SrcDir,
    [Parameter(Mandatory=$true)][string]$ExtractDir,
    [Parameter(Mandatory=$true)][string]$OutPkg,
    [Parameter(Mandatory=$true)][string]$OutDir
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$src = @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public class W32 {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);

    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
    [DllImport("kernel32.dll")] public static extern IntPtr VirtualAllocEx(IntPtr h, IntPtr addr, IntPtr size, uint type, uint protect);
    [DllImport("kernel32.dll")] public static extern bool VirtualFreeEx(IntPtr h, IntPtr addr, IntPtr size, uint type);
    [DllImport("kernel32.dll")] public static extern bool WriteProcessMemory(IntPtr h, IntPtr addr, byte[] buf, IntPtr size, out IntPtr written);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);

    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }

    public static string Txt(IntPtr h) { return TxtN(h, 1024); }
    public static string TxtN(IntPtr h, int cap) {
        StringBuilder sb = new StringBuilder(cap);
        GetWindowTextW(h, sb, cap);
        return sb.ToString();
    }
    public static string Cls(IntPtr h) {
        StringBuilder sb = new StringBuilder(256);
        GetClassNameW(h, sb, sb.Capacity);
        return sb.ToString();
    }
    public static string RectOf(IntPtr h) {
        RECT r; GetWindowRect(h, out r);
        return string.Format("({0},{1})-({2},{3}) {4}x{5}", r.Left, r.Top, r.Right, r.Bottom,
            r.Right - r.Left, r.Bottom - r.Top);
    }
    public static List<string> Walk(IntPtr root) {
        List<string> res = new List<string>();
        EnumChildWindows(root, delegate(IntPtr h, IntPtr l) {
            res.Add(string.Format("  child id={0,-6} class={1,-22} vis={2,-5} en={3,-5} {4} text={5}",
                GetDlgCtrlID(h), Cls(h), IsWindowVisible(h), IsWindowEnabled(h), RectOf(h), Txt(h)));
            return true;
        }, IntPtr.Zero);
        return res;
    }
    public static List<string> TopOf(uint pid) {
        List<string> res = new List<string>();
        EnumWindows(delegate(IntPtr h, IntPtr l) {
            uint q; GetWindowThreadProcessId(h, out q);
            if (q == pid) {
                res.Add(string.Format("  top hwnd={0} class={1} vis={2} en={3} text={4}",
                    h, Cls(h), IsWindowVisible(h), IsWindowEnabled(h), Txt(h)));
            }
            return true;
        }, IntPtr.Zero);
        return res;
    }
    public static IntPtr FindDlgOf(uint pid, string cls, IntPtr skip) {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr h, IntPtr l) {
            uint q; GetWindowThreadProcessId(h, out q);
            if (q == pid && h != skip && Cls(h) == cls && IsWindowVisible(h)) { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    // the file-name / folder-name field of a shell dialog: an Edit nested in a combo, else the widest visible Edit
    public static IntPtr NameEdit(IntPtr dlg) {
        IntPtr best = IntPtr.Zero; long bestScore = -1;
        EnumChildWindows(dlg, delegate(IntPtr h, IntPtr l) {
            if (Cls(h) != "Edit" || !IsWindowVisible(h) || !IsWindowEnabled(h)) return true;
            long score = 0;
            IntPtr par = GetParent(h);
            if (par != IntPtr.Zero) {
                string pc = Cls(par);
                if (pc == "ComboBox" || pc == "ComboBoxEx32") score += 100000000;
            }
            RECT r; GetWindowRect(h, out r);
            score += ((long)(r.Right - r.Left)) * (r.Bottom - r.Top);
            if (score > bestScore) { bestScore = score; best = h; }
            return true;
        }, IntPtr.Zero);
        return best;
    }
    public static string Body(IntPtr dlg) {
        List<string> parts = new List<string>();
        EnumChildWindows(dlg, delegate(IntPtr h, IntPtr l) {
            string c = Cls(h);
            if (!IsWindowVisible(h)) return true;
            if (c == "Static" || c == "Edit" || c == "Button") {
                string t = Txt(h);
                if (t.Length > 0) parts.Add(t);
            }
            return true;
        }, IntPtr.Zero);
        return string.Join(" / ", parts.ToArray());
    }
    public static bool SetTextCross(IntPtr hwnd, uint pid, string text) {
        const uint PROCESS_VM_OPERATION = 0x0008, PROCESS_VM_WRITE = 0x0020, PROCESS_VM_READ = 0x0010;
        IntPtr h = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, false, pid);
        if (h == IntPtr.Zero) return false;
        byte[] buf = Encoding.Unicode.GetBytes(text + "\0");
        IntPtr remote = VirtualAllocEx(h, IntPtr.Zero, (IntPtr)buf.Length, 0x1000 | 0x2000, 0x04);
        if (remote == IntPtr.Zero) { CloseHandle(h); return false; }
        IntPtr written;
        bool ok = WriteProcessMemory(h, remote, buf, (IntPtr)buf.Length, out written);
        if (ok) { SendMessage(hwnd, 0x000C, IntPtr.Zero, remote); }   // WM_SETTEXT
        VirtualFreeEx(h, remote, IntPtr.Zero, 0x8000);
        CloseHandle(h);
        return ok;
    }
}
'@
Add-Type -TypeDefinition $src -Language CSharp

$log = New-Object System.Collections.Generic.List[string]
function Log([string]$m) { $log.Add($m); Write-Host $m }
function Save() { [System.IO.File]::WriteAllText((Join-Path $OutDir 'gui_e2e.txt'), ($log -join "`r`n"), [System.Text.Encoding]::UTF8) }

$hMain = [IntPtr]::Zero
$OutDir = (Resolve-Path $OutDir).Path

function CaptureWin([IntPtr]$hwnd, [string]$name) {
    $path = Join-Path $OutDir $name
    $r = New-Object W32+RECT
    [void][W32]::GetWindowRect($hwnd, [ref]$r)
    $w = $r.Right - $r.Left
    $h = $r.Bottom - $r.Top
    if ($w -le 0 -or $h -le 0) { Log "capture skipped (empty) $name"; return }
    $bmp = New-Object System.Drawing.Bitmap($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size($w, $h)))
    $g.Dispose()
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Log ("capture {0} ({1}x{2})" -f $name, $w, $h)
}

function ListCount() {
    return [W32]::SendMessage([W32]::GetDlgItem($hMain, 1011), 0x1004, [IntPtr]::Zero, [IntPtr]::Zero)
}

function Snapshot([string]$tag) {
    Log ("[$tag] status=" + [W32]::Txt([W32]::GetDlgItem($hMain, 1014)))
    Log ("[$tag] listcount=" + (ListCount))
    Log ("[$tag] filter=" + [W32]::Txt([W32]::GetDlgItem($hMain, 1007)))
    $cs = [W32]::SendMessage([W32]::GetDlgItem($hMain, 1008), 0x00F0, [IntPtr]::Zero, [IntPtr]::Zero)
    $ce = [W32]::SendMessage([W32]::GetDlgItem($hMain, 1009), 0x00F0, [IntPtr]::Zero, [IntPtr]::Zero)
    $ct = [W32]::SendMessage([W32]::GetDlgItem($hMain, 1010), 0x00F0, [IntPtr]::Zero, [IntPtr]::Zero)
    Log ("[$tag] checks compress=" + $cs + " encrypt=" + $ce + " template=" + $ct)
    $d = [W32]::TxtN([W32]::GetDlgItem($hMain, 1012), 8000)
    Log ("[$tag] detail<" + $d.Length + " chars>=" + (($d -split "`r`n" | Select-Object -First 6) -join ' | '))
}

function PostCommand([int]$id, [string]$name) {
    $h = [W32]::GetDlgItem($hMain, $id)
    Log ("post WM_COMMAND {0} (id={1}) ctrl hwnd={2} enabled={3}" -f $name, $id, $h, [W32]::IsWindowEnabled($h))
    [void][W32]::PostMessage($hMain, 0x0111, [IntPtr]$id, $h)
}

function WaitDlg([string]$what, [int]$timeoutMs, [IntPtr]$skip) {
    $deadline = (Get-Date).AddMilliseconds($timeoutMs)
    do {
        Start-Sleep -Milliseconds 300
        $d = [W32]::FindDlgOf($p.Id, '#32770', $skip)
        if ($d -ne [IntPtr]::Zero) { return $d }
    } while ((Get-Date) -lt $deadline)
    Log "  TIMEOUT waiting for $what"
    return [IntPtr]::Zero
}

function WaitGone([IntPtr]$hwnd, [int]$timeoutMs) {
    $deadline = (Get-Date).AddMilliseconds($timeoutMs)
    do {
        Start-Sleep -Milliseconds 300
        if (-not [W32]::IsWindow($hwnd) -or -not [W32]::IsWindowVisible($hwnd)) { return $true }
    } while ((Get-Date) -lt $deadline)
    return $false
}

function FillAndOk([IntPtr]$dlg, [string]$text, [string]$what) {
    $ed = [W32]::NameEdit($dlg)
    Log ("  {0}: name edit hwnd={1} class={2} rect={3}" -f $what, $ed, [W32]::Cls($ed), [W32]::RectOf($ed))
    if ($ed -eq [IntPtr]::Zero) { return $false }
    $ok = [W32]::SetTextCross($ed, $p.Id, $text)
    Start-Sleep -Milliseconds 400
    Log ("  set text ok={0}; edit now reads: {1}" -f $ok, [W32]::Txt($ed))
    $btn = [W32]::GetDlgItem($dlg, 1)
    Log ("  OK button hwnd={0}" -f $btn)
    if ($btn -ne [IntPtr]::Zero) {
        [void][W32]::PostMessage($dlg, 0x0111, [IntPtr]1, $btn)
    } else {
        [void][W32]::PostMessage($ed, 0x0100, [IntPtr]0x0D, [IntPtr]::Zero)   # WM_KEYDOWN VK_RETURN
        [void][W32]::PostMessage($ed, 0x0101, [IntPtr]0x0D, [IntPtr]::Zero)   # WM_KEYUP
    }
    return $true
}

$p = Start-Process -FilePath $Exe -PassThru
for ($i = 0; $i -lt 60; $i++) {
    Start-Sleep -Milliseconds 250
    $p.Refresh()
    if ($p.HasExited) { Log "exited early code=$($p.ExitCode)"; Save; exit 1 }
    if ($p.MainWindowHandle -ne 0) { $hMain = $p.MainWindowHandle; break }
}
Log "pid=$($p.Id) main hwnd=$hMain"
if ($hMain -eq [IntPtr]::Zero) { Save; exit 1 }
Start-Sleep -Milliseconds 900
[void][W32]::SetForegroundWindow($hMain)
Start-Sleep -Milliseconds 300
CaptureWin $hMain 'gui_1_startup.png'
Log ("title=" + [W32]::Txt($hMain) + " class=" + [W32]::Cls($hMain))
Log "--- main window children ---"
[W32]::Walk($hMain) | ForEach-Object { Log $_ }
Snapshot 'startup'

# ---------- 1. usage help ----------
PostCommand 1004 'usage-help'
$dlg = WaitDlg 'usage dialog' 8000 ([IntPtr]::Zero)
if ($dlg -ne [IntPtr]::Zero) {
    [void][W32]::SetForegroundWindow($dlg)
    Start-Sleep -Milliseconds 500
    CaptureWin $dlg 'gui_2_usage.png'
    Log ("usage dialog title=" + [W32]::Txt($dlg))
    Log ("usage dialog body=" + [W32]::Body($dlg))
    [void][W32]::PostMessage($dlg, 0x0111, [IntPtr]1, [W32]::GetDlgItem($dlg, 1))
    [void](WaitGone $dlg 6000)
    Log "usage dialog closed"
}

# ---------- 2. open package ----------
PostCommand 1001 'open-pkg'
$openDlg = WaitDlg 'open dialog' 15000 ([IntPtr]::Zero)
if ($openDlg -ne [IntPtr]::Zero) {
    [void][W32]::SetForegroundWindow($openDlg)
    Start-Sleep -Milliseconds 600
    CaptureWin $openDlg 'gui_3_filedialog.png'
    Log ("open dialog hwnd={0} title={1}" -f $openDlg, [W32]::Txt($openDlg))
    [W32]::Walk($openDlg) | ForEach-Object { Log $_ }
    [void](FillAndOk $openDlg $Pkg 'open')
    $closed = WaitGone $openDlg 15000
    Log "open dialog closed=$closed"
    Start-Sleep -Milliseconds 1200
    CaptureWin $hMain 'gui_4_opened.png'
    Snapshot 'after-open'
} else {
    Log "open dialog did not appear"
    CaptureWin $hMain 'gui_4_no_open.png'
}

# ---------- 3. extract ----------
PostCommand 1002 'extract-to-dir'
$pick = WaitDlg 'extract folder picker' 15000 ([IntPtr]::Zero)
if ($pick -ne [IntPtr]::Zero) {
    [void][W32]::SetForegroundWindow($pick)
    Start-Sleep -Milliseconds 600
    CaptureWin $pick 'gui_5_extract_pick.png'
    Log ("extract picker hwnd={0} title={1}" -f $pick, [W32]::Txt($pick))
    [void](FillAndOk $pick $ExtractDir 'extract')
    Log ("extract picker closed=" + (WaitGone $pick 15000))
    $done = WaitDlg 'extract result message box' 60000 ([IntPtr]::Zero)
    if ($done -ne [IntPtr]::Zero) {
        [void][W32]::SetForegroundWindow($done)
        Start-Sleep -Milliseconds 500
        CaptureWin $done 'gui_6_extract_done.png'
        Log ("extract result title=" + [W32]::Txt($done))
        Log ("extract result body=" + [W32]::Body($done))
        [void][W32]::PostMessage($done, 0x0111, [IntPtr]1, [W32]::GetDlgItem($done, 1))
        [void](WaitGone $done 6000)
    } else {
        Log "no extract result box"
    }
    Snapshot 'after-extract'
} else {
    Log "extract folder picker did not appear"
}

# ---------- 4. pack ----------
PostCommand 1003 'pack-folder'
$pick = WaitDlg 'pack source folder picker' 15000 ([IntPtr]::Zero)
if ($pick -ne [IntPtr]::Zero) {
    [void][W32]::SetForegroundWindow($pick)
    Start-Sleep -Milliseconds 600
    CaptureWin $pick 'gui_7_pack_pick.png'
    Log ("pack source picker hwnd={0} title={1}" -f $pick, [W32]::Txt($pick))
    [void](FillAndOk $pick $SrcDir 'pack-src')
    Log ("pack source picker closed=" + (WaitGone $pick 15000))
    $save = WaitDlg 'pack save dialog' 15000 ([IntPtr]::Zero)
    if ($save -ne [IntPtr]::Zero) {
        [void][W32]::SetForegroundWindow($save)
        Start-Sleep -Milliseconds 600
        CaptureWin $save 'gui_8_pack_save.png'
        Log ("pack save hwnd={0} title={1}" -f $save, [W32]::Txt($save))
        [void](FillAndOk $save $OutPkg 'pack-out')
        Log ("pack save closed=" + (WaitGone $save 15000))
        $done = WaitDlg 'pack result message box' 60000 ([IntPtr]::Zero)
        if ($done -ne [IntPtr]::Zero) {
            [void][W32]::SetForegroundWindow($done)
            Start-Sleep -Milliseconds 500
            CaptureWin $done 'gui_9_pack_done.png'
            Log ("pack result title=" + [W32]::Txt($done))
            Log ("pack result body=" + [W32]::Body($done))
            [void][W32]::PostMessage($done, 0x0111, [IntPtr]1, [W32]::GetDlgItem($done, 1))
            [void](WaitGone $done 6000)
        } else {
            Log "no pack result box"
        }
        Snapshot 'after-pack'
    } else {
        Log "pack save dialog did not appear"
    }
} else {
    Log "pack folder picker did not appear"
}

[void][W32]::PostMessage($hMain, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)   # WM_CLOSE
Start-Sleep -Milliseconds 1200
$p.Refresh()
Log "exited=$($p.HasExited) exitCode=$($p.ExitCode)"
Save
exit 0