$port = New-Object System.IO.Ports.SerialPort COM3,9600,None,8,one

$port.DtrEnable = $true
$port.RtsEnable = $true

$port.Open()

$port.Write("FA;")

Start-Sleep -Milliseconds 500

$response = $port.ReadExisting()

Write-Host $response

$port.Close()