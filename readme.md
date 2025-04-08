# QMK Geekboards Macropad v2
![Demo](https://github.com/user-attachments/assets/9db30860-c476-4a45-b000-325fac729b7a)  
pfrankov's build

## Flash the firmware to Macropad v2
1. Install [QMK Toolbox](https://github.com/qmk/qmk_toolbox/releases)
1. Press and hold leftmost button on top row
1. Plug in USB
1. Keep holding the button for 3 more seconds
1. Release the button
1. Flash `geekboards_macropad_v2_via-pfrankov.bin` using QMK Toolbox interface.

## Keybindings
Change default keybindings to your own:
https://usevia.app/

## Build
```bash
git submodule update --init --recursive
util/qmk_install.sh
qmk compile -kb geekboards/macropad_v2 -km via-pfrankov
```
