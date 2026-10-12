param(
    [Parameter(Mandatory = $true)]
    [string]$TestExecutable,
    [int]$TimeoutSeconds = 15
)

$ErrorActionPreference = 'Stop'
$resolvedExecutable = (Resolve-Path -LiteralPath $TestExecutable).Path
$eventSuffix = [Guid]::NewGuid().ToString('N')
$readyName = "Local\SparrowCoreCtrlC.$eventSuffix.ready"
$releaseName = "Local\SparrowCoreCtrlC.$eventSuffix.release"
$observedName = "Local\SparrowCoreCtrlC.$eventSuffix.observed"
$ready = $null
$release = $null
$observed = $null
$process = $null
$parentIgnoringCtrlC = $false

if (-not ('SparrowConsoleCtrlCApi' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class SparrowConsoleCtrlCApi
{
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool GenerateConsoleCtrlEvent(uint ctrlEvent, uint processGroupId);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool SetConsoleCtrlHandler(IntPtr handler, bool add);
}
'@
}

try {
    $ready = [System.Threading.EventWaitHandle]::new(
        $false,
        [System.Threading.EventResetMode]::ManualReset,
        $readyName)
    $release = [System.Threading.EventWaitHandle]::new(
        $false,
        [System.Threading.EventResetMode]::ManualReset,
        $releaseName)
    $observed = [System.Threading.EventWaitHandle]::new(
        $false,
        [System.Threading.EventResetMode]::ManualReset,
        $observedName)

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $resolvedExecutable
    $startInfo.Arguments = "--windows-console-ctrlc-probe $readyName $releaseName $observedName"
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true

    # No CREATE_NEW_CONSOLE or CREATE_NEW_PROCESS_GROUP: CTRL_C_EVENT must reach
    # the child through the same console, and Windows cannot target it by group.
    $process = [System.Diagnostics.Process]::Start($startInfo)
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()

    # Do this after child creation so the child's inherited Ctrl-C behavior stays enabled.
    if (-not [SparrowConsoleCtrlCApi]::SetConsoleCtrlHandler([IntPtr]::Zero, $true)) {
        $errorCode = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        throw "Cannot make the launcher ignore Ctrl-C (Win32 error $errorCode)"
    }
    $parentIgnoringCtrlC = $true

    if (-not $ready.WaitOne([TimeSpan]::FromSeconds($TimeoutSeconds))) {
        throw "Probe did not reach its active exploration callback within $TimeoutSeconds seconds"
    }

    if (-not [SparrowConsoleCtrlCApi]::GenerateConsoleCtrlEvent(0, 0)) {
        $errorCode = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        throw "GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0) failed (Win32 error $errorCode)"
    }
    $release.Set() | Out-Null

    if (-not $observed.WaitOne([TimeSpan]::FromSeconds(5))) {
        throw 'The optimizer did not observe the Windows console Ctrl-C request'
    }
    if (-not $process.WaitForExit(15000)) {
        throw 'The probe did not finish its Rust-reference trace after Ctrl-C'
    }

    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    if ($process.ExitCode -ne 0) {
        throw "Probe failed with exit code $($process.ExitCode). stdout: $stdout stderr: $stderr"
    }
    if (($stdout + $stderr) -notmatch 'WINDOWS_CTRL_C_PROBE_PASS') {
        throw "Probe exited without its pass marker. stdout: $stdout stderr: $stderr"
    }

    if (-not [string]::IsNullOrWhiteSpace($stdout)) {
        Write-Output $stdout.TrimEnd()
    }
    if (-not [string]::IsNullOrWhiteSpace($stderr)) {
        Write-Output $stderr.TrimEnd()
    }
    Write-Output 'Windows console CTRL_C_EVENT subprocess probe passed'
}
finally {
    if ($null -ne $release) {
        $release.Set() | Out-Null
    }
    if ($null -ne $process) {
        $process.Refresh()
        if (-not $process.HasExited) {
            $process.Kill()
            $process.WaitForExit()
        }
        $process.Dispose()
    }
    if ($parentIgnoringCtrlC) {
        [SparrowConsoleCtrlCApi]::SetConsoleCtrlHandler([IntPtr]::Zero, $false) | Out-Null
    }
    foreach ($eventHandle in @($ready, $release, $observed)) {
        if ($null -ne $eventHandle) {
            $eventHandle.Dispose()
        }
    }
}
