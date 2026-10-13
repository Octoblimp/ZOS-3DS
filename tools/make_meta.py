"""Make the CIA's icon and banner from the APK's own art.

    python tools/make_meta.py

Writes 3DS Files/meta/icon.png (48x48, the launcher icon) and
3DS Files/meta/banner.png (256x128, the logo off the home screen's backdrop).
"""

import os

from PIL import Image

import apk

META = os.path.join(apk.ROOT, "3DS Files", "meta")

# The home screen's 1024 backdrop has the Z.O.S logo centred on
# about (510, 370). The banner is a 2:1 window around it.
BANNER_BOX = (150, 190, 870, 550)


def main():
    root = apk.extracted()
    icon = Image.open(os.path.join(root, "res", "mipmap-xxxhdpi-v4", "ic_launcher.png")).convert("RGBA")
    icon.resize((48, 48), Image.LANCZOS).save(os.path.join(META, "icon.png"))

    home = Image.open(apk.asset("images", "Fullscreen", "1024", "Background_HomeScreen.png")).convert("RGBA")
    home.crop(BANNER_BOX).resize((256, 128), Image.LANCZOS).save(os.path.join(META, "banner.png"))
    print("wrote meta/icon.png, meta/banner.png")


if __name__ == "__main__":
    main()
