param(
  [Parameter(Mandatory = $true)]
  [string]$Ncat
)

$ErrorActionPreference = "Stop"

function Invoke-NcatExchange {
  param(
    [string]$VerboseArgument,
    [string]$ModeArgument,
    [byte[]]$InputPayload,
    [byte[]]$OutputPayload
  )

  $listener = [System.Net.Sockets.TcpListener]::new(
    [System.Net.IPAddress]::Loopback,
    0
  )
  $listener.Start()
  $port = ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port
  $acceptTask = $listener.AcceptTcpClientAsync()

  $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
  $startInfo.FileName = $Ncat
  $startInfo.UseShellExecute = $false
  $startInfo.CreateNoWindow = $true
  $startInfo.RedirectStandardInput = $true
  $startInfo.RedirectStandardOutput = $true
  $startInfo.RedirectStandardError = $true
  $startInfo.Arguments = (
    $VerboseArgument,
    "127.0.0.1:$port",
    $ModeArgument
  ) -join " "

  $process = [System.Diagnostics.Process]::new()
  $process.StartInfo = $startInfo
  [void]$process.Start()
  $stdout = [System.IO.MemoryStream]::new()
  $stdoutTask = $process.StandardOutput.BaseStream.CopyToAsync($stdout)
  $stderrTask = $process.StandardError.ReadToEndAsync()

  if (-not $acceptTask.Wait(10000)) {
    if (-not $process.HasExited) {
      $process.Kill()
      $process.WaitForExit()
    }
    $stderr = $stderrTask.GetAwaiter().GetResult()
    throw "rstream-ncat did not connect within 10 seconds: $stderr"
  }
  $peer = $acceptTask.Result
  $stream = $peer.GetStream()
  try {
    if ($InputPayload.Length -gt 0) {
      $process.StandardInput.BaseStream.Write(
        $InputPayload,
        0,
        $InputPayload.Length
      )
      $process.StandardInput.BaseStream.Flush()
      $process.StandardInput.Close()

      $received = [byte[]]::new($InputPayload.Length)
      $offset = 0
      while ($offset -lt $received.Length) {
        $read = $stream.Read(
          $received,
          $offset,
          $received.Length - $offset
        )
        if ($read -eq 0) {
          throw "rstream-ncat closed before forwarding all standard input"
        }
        $offset += $read
      }
      if ([Convert]::ToBase64String($InputPayload) -ne
        [Convert]::ToBase64String($received)) {
        throw "rstream-ncat changed the standard input payload"
      }
    }
    else {
      $process.StandardInput.Close()
    }

    $stream.Write($OutputPayload, 0, $OutputPayload.Length)
    $stream.Flush()
    $peer.Client.Shutdown([System.Net.Sockets.SocketShutdown]::Send)

    if (-not $process.WaitForExit(10000)) {
      $process.Kill()
      throw "rstream-ncat did not stop after a graceful peer close"
    }
    [void]$stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    $actualOutput = $stdout.ToArray()
    if ($process.ExitCode -ne 0) {
      throw "rstream-ncat exited with $($process.ExitCode): $stderr"
    }
    if ([Convert]::ToBase64String($OutputPayload) -ne
      [Convert]::ToBase64String($actualOutput)) {
      $expected = [Convert]::ToBase64String($OutputPayload)
      $actual = [Convert]::ToBase64String($actualOutput)
      throw "rstream-ncat changed standard output: expected $($OutputPayload.Length) bytes ($expected), got $($actualOutput.Length) bytes ($actual)"
    }
  }
  finally {
    $stream.Dispose()
    $peer.Dispose()
    $listener.Stop()
    $stdout.Dispose()
    $process.Dispose()
  }
}

$utf8 = [System.Text.Encoding]::UTF8
Invoke-NcatExchange `
  -VerboseArgument "-v" `
  -ModeArgument "-I" `
  -InputPayload ([byte[]]::new(0)) `
  -OutputPayload $utf8.GetBytes("server-output")
Invoke-NcatExchange `
  -VerboseArgument "--verbose" `
  -ModeArgument "-i" `
  -InputPayload $utf8.GetBytes("client-input") `
  -OutputPayload $utf8.GetBytes("server-output")
