# uinput-titan-pocket

This is a tool to bring back touchpad functionality on the Unihertz Titan Pocket when using a custom ROM (and potentially have some other benefits/functionality too).

This code is a derivation of an older version of https://github.com/phhusson/unihertz_titan for the Unihertz Titan (non pocket). I have made various changes and adjustments to the way it works.

The way it currently works is by forwarding all touch events (except for single taps) to the uinput device and rebounding them to a smaller region of the screen, this way they will _probably_ be used for scrolling and not accidentally opening the navbar or anything. Unfortunately Android sometimes will also recognize swipes as a tap if they are small enough, and this tool does not yet work around this.

## Building

```sh
$ANDROID_NDK_BUNDLE/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang uinput-titan-pocket.c -o uinput-titan-pocket -Wall -Wextra
```

## Testing (does not need ADB root)

```sh
adb push uinput-titan-pocket /sdcard/uinput-titan-pocket
adb shell 'su -c "mv /sdcard/uinput-titan-pocket /data/local/tmp/uinput-titan-pocket"'
adb shell 'su -c "chmod +x /data/local/tmp/uinput-titan-pocket"'
adb exec-out 'su -c /data/local/tmp/uinput-titan-pocket'
```

## Installing (requires ADB root)

```sh
adb push uinput-titan-pocket /system/bin/uinput-titan-pocket
```

Adding an init file to `/system/etc/init` will not work due to SELINUX, I have not bothered to find a way around this, so you will have to launch it manually, or using [Termux:boot](https://github.com/termux/termux-boot) or something.
