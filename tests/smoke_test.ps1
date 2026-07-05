param([int]$Port = 8766)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$serverPath = Join-Path $root 'chat_server.exe'

if (-not (Test-Path -LiteralPath $serverPath)) {
    throw 'chat_server.exe is missing. Run build.ps1 first.'
}

$adminPassword = 'smoke-admin'
$testTrackName = 'smoke-test.wav'
$testTrackPath = Join-Path $root "data\music\$testTrackName"
$suffix = ([DateTime]::UtcNow.Ticks.ToString()).Substring(10)
$guestName = "Cpp$suffix"
$webName = "Web$suffix"
$accountName = "Acct$suffix"
$contactName = "Peer$suffix"
$accountPassword = 'Secure-Test-2026'
$process = Start-Process -FilePath $serverPath -ArgumentList '--port', $Port, '--admin-password', $adminPassword -WorkingDirectory $root -PassThru -WindowStyle Hidden
try {
    $ready = $false
    for ($attempt = 0; $attempt -lt 30; $attempt++) {
        try {
            $health = Invoke-RestMethod "http://127.0.0.1:$Port/api/health" -TimeoutSec 1
            $ready = [bool]$health.ok
            break
        }
        catch { Start-Sleep -Milliseconds 200 }
    }
    if (-not $ready) { throw 'Server health check failed.' }

    $index = Invoke-WebRequest "http://127.0.0.1:$Port/" -UseBasicParsing
    if ($index.StatusCode -ne 200) { throw 'Static page request failed.' }
    $website = Invoke-WebRequest "http://127.0.0.1:$Port/web" -UseBasicParsing
    if ($website.StatusCode -ne 200 -or $website.Content -notmatch 'SHARED CHAT CHANNEL') { throw 'Website page request failed.' }
    $websiteScript = Invoke-WebRequest "http://127.0.0.1:$Port/web.js" -UseBasicParsing
    if ($websiteScript.StatusCode -ne 200) { throw 'Website script request failed.' }
    $scene = Invoke-WebRequest "http://127.0.0.1:$Port/images/scene-01.jpg" -UseBasicParsing
    if ($scene.StatusCode -ne 200 -or $scene.RawContentLength -lt 1000) { throw 'Scene image request failed.' }

    $joinBody = @{ name = $guestName } | ConvertTo-Json -Compress
    $joined = Invoke-RestMethod "http://127.0.0.1:$Port/api/join" -Method Post -ContentType 'application/json' -Body $joinBody
    if (-not $joined.clientId) { throw 'Join API failed.' }

    $messageBody = @{ clientId = $joined.clientId; text = '/dict network' } | ConvertTo-Json -Compress
    Invoke-RestMethod "http://127.0.0.1:$Port/api/messages" -Method Post -ContentType 'application/json' -Body $messageBody | Out-Null
    $snapshot = Invoke-RestMethod "http://127.0.0.1:$Port/api/messages?clientId=$($joined.clientId)&after=0"
    $botReply = $snapshot.messages | Where-Object { $_.kind -eq 'bot' -and $_.text -match 'network' }
    if ($null -eq $botReply) { throw 'Dictionary bot response was not found.' }

    $lookup = Invoke-RestMethod "http://127.0.0.1:$Port/api/dictionary?clientId=$($joined.clientId)&q=ability"
    if ($lookup.query -ne 'ability' -or $lookup.result -notmatch 'ability') { throw 'Direct dictionary lookup failed.' }

    $dictionaryHistory = Invoke-RestMethod "http://127.0.0.1:$Port/api/history?clientId=$($joined.clientId)&type=dictionary&q=ability"
    if ($dictionaryHistory.count -lt 1) { throw 'Dictionary history query failed.' }

    $chatHistory = Invoke-RestMethod "http://127.0.0.1:$Port/api/history?clientId=$($joined.clientId)&type=chat&q=network"
    if ($chatHistory.count -lt 1) { throw 'Chat history query failed.' }

    $webJoinBody = @{ name = $webName } | ConvertTo-Json -Compress
    $webJoined = Invoke-RestMethod "http://127.0.0.1:$Port/api/join" -Method Post -ContentType 'application/json' -Body $webJoinBody
    $webMessageBody = @{ clientId = $webJoined.clientId; text = 'message-from-website' } | ConvertTo-Json -Compress
    Invoke-RestMethod "http://127.0.0.1:$Port/api/messages" -Method Post -ContentType 'application/json' -Body $webMessageBody | Out-Null
    $miniSnapshot = Invoke-RestMethod "http://127.0.0.1:$Port/api/messages?clientId=$($joined.clientId)&after=0"
    if ($miniSnapshot.messages.text -notcontains 'message-from-website') { throw 'Website to mini-app communication failed.' }

    $miniMessageBody = @{ clientId = $joined.clientId; text = 'message-from-mini-app' } | ConvertTo-Json -Compress
    Invoke-RestMethod "http://127.0.0.1:$Port/api/messages" -Method Post -ContentType 'application/json' -Body $miniMessageBody | Out-Null
    $webSnapshot = Invoke-RestMethod "http://127.0.0.1:$Port/api/messages?clientId=$($webJoined.clientId)&after=0"
    if ($webSnapshot.messages.text -notcontains 'message-from-mini-app') { throw 'Mini-app to website communication failed.' }

    $scoreBody = @{ clientId = $joined.clientId; score = 1280 } | ConvertTo-Json -Compress
    Invoke-RestMethod "http://127.0.0.1:$Port/api/game/scores" -Method Post -ContentType 'application/json' -Body $scoreBody | Out-Null
    $scores = Invoke-RestMethod "http://127.0.0.1:$Port/api/game/scores"
    $testScore = $scores.scores | Where-Object { $_.name -eq $guestName -and $_.score -eq 1280 }
    if ($null -eq $testScore) { throw 'Online game leaderboard failed.' }

    $registerBody = @{ name = $accountName; password = $accountPassword } | ConvertTo-Json -Compress
    $account = Invoke-RestMethod "http://127.0.0.1:$Port/api/register" -Method Post -ContentType 'application/json' -Body $registerBody
    if ($account.authMode -ne 'account') { throw 'Account registration failed.' }
    $peerBody = @{ name = $contactName; password = $accountPassword } | ConvertTo-Json -Compress
    $peer = Invoke-RestMethod "http://127.0.0.1:$Port/api/register" -Method Post -ContentType 'application/json' -Body $peerBody

    $rooms = Invoke-RestMethod "http://127.0.0.1:$Port/api/rooms?clientId=$($account.clientId)"
    if ($rooms.rooms.id -notcontains 'lobby' -or $rooms.rooms.id -notcontains 'english') { throw 'Default room list failed.' }
    $roomName = "TestRoom$suffix"
    $roomBody = @{ clientId = $account.clientId; name = $roomName } | ConvertTo-Json -Compress
    $createdRoom = Invoke-RestMethod "http://127.0.0.1:$Port/api/rooms" -Method Post -ContentType 'application/json' -Body $roomBody
    $roomMessageBody = @{ clientId = $account.clientId; roomId = $createdRoom.id; text = 'persistent-room-message' } | ConvertTo-Json -Compress
    Invoke-RestMethod "http://127.0.0.1:$Port/api/messages" -Method Post -ContentType 'application/json' -Body $roomMessageBody | Out-Null
    $roomSnapshot = Invoke-RestMethod "http://127.0.0.1:$Port/api/messages?clientId=$($account.clientId)&roomId=$($createdRoom.id)&after=0"
    if ($roomSnapshot.messages.text -notcontains 'persistent-room-message') { throw 'Multi-room messaging failed.' }

    $dmBody = @{ clientId = $peer.clientId; recipient = $accountName; text = 'private-message-test' } | ConvertTo-Json -Compress
    Invoke-RestMethod "http://127.0.0.1:$Port/api/messages" -Method Post -ContentType 'application/json' -Body $dmBody | Out-Null
    $contactsBefore = Invoke-RestMethod "http://127.0.0.1:$Port/api/contacts?clientId=$($account.clientId)"
    $peerContact = $contactsBefore.contacts | Where-Object { $_.name -eq $contactName }
    if ($null -eq $peerContact -or $peerContact.unread -lt 1) { throw 'Private message unread count failed.' }
    $dmSnapshot = Invoke-RestMethod "http://127.0.0.1:$Port/api/messages?clientId=$($account.clientId)&recipient=$contactName&after=0"
    if ($dmSnapshot.messages.text -notcontains 'private-message-test') { throw 'Private messaging failed.' }
    $contactsAfter = Invoke-RestMethod "http://127.0.0.1:$Port/api/contacts?clientId=$($account.clientId)"
    if (($contactsAfter.contacts | Where-Object { $_.name -eq $contactName }).unread -ne 0) { throw 'Unread reset failed.' }

    $wavBytes = [Convert]::FromBase64String('UklGRiQAAABXQVZFZm10IBAAAAABAAEAQB8AAEAfAAABAAgAZGF0YQAAAAA=')
    $adminHeaders = @{ 'X-Admin-Password' = $adminPassword }
    Invoke-RestMethod "http://127.0.0.1:$Port/api/admin/music?name=$testTrackName" -Method Post -Headers $adminHeaders -ContentType 'application/octet-stream' -Body $wavBytes | Out-Null
    $music = Invoke-RestMethod "http://127.0.0.1:$Port/api/music"
    if ($music.tracks.name -notcontains $testTrackName) { throw 'Music upload/list API failed.' }

    Add-Type -AssemblyName System.Net.Http
    $httpClient = [Net.Http.HttpClient]::new()
    $rangeRequest = [Net.Http.HttpRequestMessage]::new([Net.Http.HttpMethod]::Get, "http://127.0.0.1:$Port/api/music/file?name=$testTrackName")
    $rangeRequest.Headers.Range = [Net.Http.Headers.RangeHeaderValue]::new(0, 15)
    $rangeResponse = $httpClient.SendAsync($rangeRequest).GetAwaiter().GetResult()
    $rangeBytes = $rangeResponse.Content.ReadAsByteArrayAsync().GetAwaiter().GetResult()
    if ([int]$rangeResponse.StatusCode -ne 206 -or $rangeBytes.Length -ne 16) { throw 'Music range streaming failed.' }
    $rangeRequest.Dispose()
    $rangeResponse.Dispose()
    $httpClient.Dispose()

    Invoke-RestMethod "http://127.0.0.1:$Port/api/admin/music?name=$testTrackName" -Method Delete -Headers $adminHeaders | Out-Null
    $musicAfterDelete = Invoke-RestMethod "http://127.0.0.1:$Port/api/music"
    if ($musicAfterDelete.tracks.name -contains $testTrackName) { throw 'Music delete API failed.' }

    Stop-Process -Id $process.Id -Force
    $process.WaitForExit()
    $process = Start-Process -FilePath $serverPath -ArgumentList '--port', $Port, '--admin-password', $adminPassword -WorkingDirectory $root -PassThru -WindowStyle Hidden
    $ready = $false
    for ($attempt = 0; $attempt -lt 30; $attempt++) {
        try { $ready = [bool](Invoke-RestMethod "http://127.0.0.1:$Port/api/health" -TimeoutSec 1).ok; if ($ready) { break } }
        catch { Start-Sleep -Milliseconds 200 }
    }
    if (-not $ready) { throw 'Server restart health check failed.' }
    $loginBody = @{ name = $accountName; password = $accountPassword } | ConvertTo-Json -Compress
    $loggedIn = Invoke-RestMethod "http://127.0.0.1:$Port/api/login" -Method Post -ContentType 'application/json' -Body $loginBody
    $persistedRooms = Invoke-RestMethod "http://127.0.0.1:$Port/api/rooms?clientId=$($loggedIn.clientId)"
    if ($persistedRooms.rooms.name -notcontains $roomName) { throw 'Room persistence failed.' }
    $persistedMessages = Invoke-RestMethod "http://127.0.0.1:$Port/api/messages?clientId=$($loggedIn.clientId)&roomId=$($createdRoom.id)&after=0"
    if ($persistedMessages.messages.text -notcontains 'persistent-room-message') { throw 'Chat persistence failed.' }

    Write-Host 'C++ server smoke test passed.' -ForegroundColor Green
}
finally {
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
    if (Test-Path -LiteralPath $testTrackPath) { Remove-Item -LiteralPath $testTrackPath -Force }
}
