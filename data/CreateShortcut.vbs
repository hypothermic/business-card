Set WshShell = CreateObject("WScript.Shell")

Set Shortcut = WshShell.CreateShortcut("LinkedIn.lnk")
Shortcut.TargetPath = "https://linkedin.com/in/matthijs-p-bakker"
'Shortcut.IconLocation = "data\LinkedIn.ico"
Shortcut.Save

Set Shortcut = WshShell.CreateShortcut("GitHub.lnk")
Shortcut.TargetPath = "https://github.com/hypothermic/business-card/"
'Shortcut.IconLocation = "data\GitHub.ico"
Shortcut.Save
