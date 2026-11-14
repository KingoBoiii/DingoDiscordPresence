<#
.SYNOPSIS
  A fake Discord for captures and by-hand tests. PowerShell 5.1 compatible.
.DESCRIPTION
  Hosts \\.\pipe\discord-ipc-<Index> like Discord's local RPC, reads each frame (little-endian uint32 opcode,
  uint32 length, then UTF-8 JSON) and appends "<HH:mm:ss.fff> op=<n> <json>" to -Log; the reviewer reads the JSON
  from that log. It answers the handshake (op 0) with a READY frame and every SET_ACTIVITY (op 1) with an empty reply,
  sends one PING (op 3) after the first activity (the client answers op 4), and exits on CLOSE (op 2), when the client
  disconnects or after -Seconds. A named pipe is machine-wide, so the hidden capture desktop
  needn't be the fake's. Index 9 is where a real Discord never is (it takes the lowest free index).

  -CloseAfterSeconds N closes the connection N seconds after the client connected, then listens again, so a client's
  reconnect and its send gate can be watched. -HangAfterSeconds N stops reading N seconds after the client connected,
  so the pipe fills and the client's write timeout fires.
.EXAMPLE
  powershell -File tools\fake-discord.ps1 -Index 9 -Log C:\scratch\pipe.log -DelaySeconds 15 -Seconds 60
.EXAMPLE
  powershell -File tools\fake-discord.ps1 -Log C:\scratch\pipe.log -CloseAfterSeconds 5 -Seconds 60
.EXAMPLE
  powershell -File tools\fake-discord.ps1 -Log C:\scratch\pipe.log -HangAfterSeconds 5 -Seconds 60
#>
param(
	[int]$Index = 9,
	[Parameter(Mandatory = $true)][string]$Log,
	[int]$DelaySeconds = 0,        # starts late, for the late-Discord test
	[int]$Seconds = 120,           # the fake's whole lifetime, from the end of the delay
	[int]$CloseAfterSeconds = 0,   # 0: never. Closes the connection, then listens again
	[int]$HangAfterSeconds = 0     # 0: never. Stops reading
)

$ErrorActionPreference = 'Stop'
$utf8 = New-Object System.Text.UTF8Encoding($false)

function Write-Log([string]$line)
{
	# The time first: the send gate test reads the gaps between SET_ACTIVITY frames from it.
	[System.IO.File]::AppendAllText($Log, (Get-Date).ToString('HH:mm:ss.fff') + ' ' + $line + "`r`n", $utf8)
}

function Get-Earliest([datetime[]]$times)
{
	$first = $times[0]
	foreach ($time in $times) { if ($time -lt $first) { $first = $time } }
	return $first
}

# Reads exactly $count bytes before $deadline; $null when the client closed, the time ran out or the pipe broke.
function Read-Exact($pipe, [int]$count, [datetime]$deadline)
{
	$buffer = New-Object byte[] $count
	$got = 0
	while ($got -lt $count)
	{
		$left = ($deadline - (Get-Date)).TotalMilliseconds
		if ($left -le 0) { return $null }
		try
		{
			$async = $pipe.BeginRead($buffer, $got, $count - $got, $null, $null)
			if (-not $async.AsyncWaitHandle.WaitOne([int]$left)) { return $null }
			$n = $pipe.EndRead($async)
		}
		catch [System.IO.IOException] { return $null }
		if ($n -le 0) { return $null }   # 0 bytes: the client disconnected
		$got += $n
	}
	return , $buffer
}

# One frame out: the 8-byte header then the JSON.
function Send-Frame($pipe, [uint32]$op, [string]$json)
{
	$body = $utf8.GetBytes($json)
	$frame = New-Object byte[] (8 + $body.Length)
	[BitConverter]::GetBytes($op).CopyTo($frame, 0)
	[BitConverter]::GetBytes([uint32]$body.Length).CopyTo($frame, 4)
	$body.CopyTo($frame, 8)
	$pipe.Write($frame, 0, $frame.Length)
	$pipe.Flush()
}

if ($DelaySeconds -gt 0) { Start-Sleep -Seconds $DelaySeconds }
$deadline = (Get-Date).AddSeconds($Seconds)
$never = [datetime]::MaxValue

# Byte mode, in and out, a single instance: a second server on the name would fail, so a stale fake shows at once.
$pipe = New-Object System.IO.Pipes.NamedPipeServerStream(
	"discord-ipc-$Index", [System.IO.Pipes.PipeDirection]::InOut, 1,
	[System.IO.Pipes.PipeTransmissionMode]::Byte, [System.IO.Pipes.PipeOptions]::Asynchronous)
Write-Log "fake: listening on discord-ipc-$Index"
try
{
	$listenAgain = $true
	while ($listenAgain)
	{
		$listenAgain = $false
		$wait = $pipe.BeginWaitForConnection($null, $null)
		$left = [int][Math]::Max(0, ($deadline - (Get-Date)).TotalMilliseconds)
		if (-not $wait.AsyncWaitHandle.WaitOne($left))
		{
			Write-Log 'fake: nobody connected, exiting'
			return
		}
		$pipe.EndWaitForConnection($wait)
		Write-Log 'fake: connected'
		$connectedAt = Get-Date
		$closeAt = $never
		if ($CloseAfterSeconds -gt 0) { $closeAt = $connectedAt.AddSeconds($CloseAfterSeconds) }
		$hangAt = $never
		if ($HangAfterSeconds -gt 0) { $hangAt = $connectedAt.AddSeconds($HangAfterSeconds) }
		$pinged = $false

		while ($true)
		{
			$header = Read-Exact $pipe 8 (Get-Earliest @($deadline, $closeAt, $hangAt))
			$payload = $null
			$op = 0
			$json = ''
			if ($null -ne $header)
			{
				$op = [BitConverter]::ToUInt32($header, 0)
				$len = [BitConverter]::ToUInt32($header, 4)
				if ($len -gt 0)
				{
					$payload = Read-Exact $pipe ([int]$len) (Get-Earliest @($deadline, $closeAt, $hangAt))
					if ($null -eq $payload) { $header = $null }
					else { $json = $utf8.GetString($payload) }
				}
			}

			if ($null -eq $header)
			{
				if ((Get-Date) -ge $hangAt -and (Get-Date) -lt $deadline)
				{
					# Not reading any more: the client's writes fill the pipe until its own timeout drops the link.
					Write-Log 'fake: hanging, not reading'
					$hangAt = $never
					$stopHanging = Get-Earliest @($deadline, $closeAt)
					while ((Get-Date) -lt $stopHanging) { Start-Sleep -Milliseconds 100 }
				}
				if ((Get-Date) -ge $closeAt -and (Get-Date) -lt $deadline)
				{
					Write-Log 'fake: closing the connection'
					$pipe.Disconnect()
					$listenAgain = $true
				}
				else
				{
					Write-Log 'fake: client gone or time up'
				}
				break
			}
			Write-Log "op=$op $json"

			if ($op -eq 0)
			{
				# The handshake: READY first, as Discord does (the client waits for it before its first activity).
				Send-Frame $pipe 1 '{"cmd":"DISPATCH","evt":"READY","data":{"v":1,"config":{},"user":{"id":"0","username":"fake"}},"nonce":null}'
			}
			elseif ($op -eq 1)
			{
				# SET_ACTIVITY: an empty reply echoing the request's nonce, which a simple regex finds.
				$nonce = 'null'
				if ($json -match '"nonce"\s*:\s*"([^"]*)"') { $nonce = '"' + $Matches[1] + '"' }
				Send-Frame $pipe 1 ('{"cmd":"SET_ACTIVITY","data":{},"evt":null,"nonce":' + $nonce + '}')
				if (-not $pinged)
				{
					# One PING after the first activity: the client answers op 4 with the same payload.
					Send-Frame $pipe 3 '{"ping":1}'
					Write-Log 'fake: sent PING'
					$pinged = $true
				}
			}
			elseif ($op -eq 2)
			{
				Write-Log 'fake: CLOSE, exiting'
				break
			}
			# op 4 (the client's PONG) and anything else is only logged above.
		}
	}
}
catch [System.IO.IOException]
{
	Write-Log 'fake: pipe broke, exiting'   # a write to a client that just left
}
finally
{
	$pipe.Dispose()
}
