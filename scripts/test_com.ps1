$port = New-Object System.IO.Ports.SerialPort COM3,9600,None,8,one

$port.ReadTimeout = 1000
$port.WriteTimeout = 1000

$port.Open()

$port.Write("FA;")

Start-Sleep -Milliseconds 500

$response = $port.ReadExisting()

Write-Host "Antwort:"
Write-Host $response

$port.Close()