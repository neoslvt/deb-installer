# DEB GUI installer
<img width="714" height="516" alt="Screenshot from 2026-02-16 09-28-37" src="https://github.com/user-attachments/assets/edb023fe-f6f1-4781-843f-fff41d08254a" />

## How to install:
Download the binary file, place it anywhere you comfortable, make the .desktop file ( ~/.local/share/applications/wizard-installer.desktop ) and edit it like so:

```
[Desktop Entry]
Name=Software Setup Wizard
Exec="YOUR_DIRECTORY_TO_installer_BINARY" %f
Type=Application
MimeType=application/vnd.debian.binary-package;
Terminal=false
Icon=system-software-install
```
