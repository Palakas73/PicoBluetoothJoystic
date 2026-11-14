param([string]$Port)
& "$PSScriptRoot\capture-8bitdo.ps1" -Port $Port `
  -ControllerLabel 'Nintendo Switch Pro Controller' -LogPrefix 'switch-pro'
exit $LASTEXITCODE
