#!/usr/bin/env bash
# Builds HorrorDon.apk (arm64) from main.cpp using raylib 5.5 + Android NDK.
# Run on Linux (e.g. GitHub Codespaces):  bash build_android.sh
set -euo pipefail

APP="HorrorDon"
PKG_DOTS="com.raylib.horror"
PKG_PATH="com/raylib/horror"
LABEL="horror don virus 1999"
SRC="${1:-main.cpp}"
API=29
BT_VER="29.0.3"
NDK_VER="26.3.11579264"          # NDK r26d
CMDTOOLS_URL="https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip"
ABI="arm64-v8a"; TRIPLE="aarch64-linux-android"; RL_ARCH="arm64"
STOREPASS="raylib"

cd "$(dirname "$0")"
ROOT="$(pwd)"
[ -f "$SRC" ] || { echo "ERROR: $SRC not found (put main.cpp next to this script)"; exit 1; }

# ---------- tools
SUDO=""; [ "$(id -u)" -ne 0 ] && SUDO="sudo"
need() { command -v "$1" >/dev/null 2>&1; }
if ! need javac || ! need keytool || ! need unzip || ! need wget || ! need make || ! need git; then
  $SUDO apt-get update -y
  $SUDO apt-get install -y openjdk-17-jdk-headless unzip wget make git
fi

# ---------- Android SDK + NDK
SDK="$ROOT/android/sdk"
if [ ! -x "$SDK/cmdline-tools/latest/bin/sdkmanager" ]; then
  mkdir -p "$SDK/cmdline-tools"
  wget -q -O /tmp/cmdtools.zip "$CMDTOOLS_URL"
  rm -rf /tmp/cmdtools && unzip -q /tmp/cmdtools.zip -d /tmp/cmdtools
  rm -rf "$SDK/cmdline-tools/latest"
  mv /tmp/cmdtools/cmdline-tools "$SDK/cmdline-tools/latest"
fi
SDKM="$SDK/cmdline-tools/latest/bin/sdkmanager"
yes | "$SDKM" --sdk_root="$SDK" --licenses >/dev/null || true
"$SDKM" --sdk_root="$SDK" "build-tools;$BT_VER" "platforms;android-$API" "ndk;$NDK_VER"

NDK="$SDK/ndk/$NDK_VER"
TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64"
BT="$SDK/build-tools/$BT_VER"
JAR="$SDK/platforms/android-$API/android.jar"
GLUE="$NDK/sources/android/native_app_glue"
CC="$TOOLCHAIN/bin/${TRIPLE}${API}-clang"
CXX="$TOOLCHAIN/bin/${TRIPLE}${API}-clang++"

# ---------- raylib for Android (built once)
mkdir -p include "lib/$ABI"
if [ ! -f "lib/$ABI/libraylib.a" ]; then
  [ -d raylib ] || git clone --depth 1 --branch 5.5 https://github.com/raysan5/raylib
  cp raylib/src/raylib.h include/
  ( cd raylib/src && make clean && \
    make PLATFORM=PLATFORM_ANDROID ANDROID_NDK="$NDK" ANDROID_ARCH="$RL_ARCH" ANDROID_API_VERSION="$API" )
  mv raylib/src/libraylib.a "lib/$ABI/"
fi

# ---------- project files (manifest, loader, icons, keystore)
B="$ROOT/android/build"
rm -rf "$B/obj" "$B/dex" "$B/lib"
mkdir -p "$B/src/$PKG_PATH" "$B/obj" "$B/dex" "$B/lib/$ABI" "$B/res"
for d in ldpi:36 mdpi:48 hdpi:72 xhdpi:96; do
  n="${d%%:*}"; s="${d##*:}"
  mkdir -p "$B/res/drawable-$n"
  cp "raylib/logo/raylib_${s}x${s}.png" "$B/res/drawable-$n/icon.png"
done

cat > "$B/src/$PKG_PATH/NativeLoader.java" <<EOF
package $PKG_DOTS;
public class NativeLoader extends android.app.NativeActivity {
    static { System.loadLibrary("main"); }
}
EOF

cat > "$B/AndroidManifest.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
        package="$PKG_DOTS"
        android:versionCode="1" android:versionName="1.0">
    <uses-sdk android:minSdkVersion="23" android:targetSdkVersion="34"/>
    <uses-feature android:glEsVersion="0x00020000" android:required="true"/>
    <application android:allowBackup="false" android:label="$LABEL"
            android:icon="@drawable/icon">
        <activity android:name="$PKG_DOTS.NativeLoader"
            android:theme="@android:style/Theme.NoTitleBar.Fullscreen"
            android:configChanges="orientation|keyboardHidden|screenSize"
            android:exported="true"
            android:screenOrientation="landscape" android:launchMode="singleTask"
            android:clearTaskOnLaunch="true">
            <meta-data android:name="android.app.lib_name" android:value="main"/>
            <intent-filter>
                <action android:name="android.intent.action.MAIN"/>
                <category android:name="android.intent.category.LAUNCHER"/>
            </intent-filter>
        </activity>
    </application>
</manifest>
EOF

[ -f android/raylib.keystore ] || keytool -genkeypair -validity 10000 \
  -dname "CN=HorrorDon,O=Android,C=US" -keystore android/raylib.keystore \
  -storepass "$STOREPASS" -keypass "$STOREPASS" -alias projectKey -keyalg RSA

# ---------- compile native code
FLAGS="-O2 -ffunction-sections -funwind-tables -fstack-protector-strong -fPIC -Wall \
 -DANDROID -DPLATFORM_ANDROID -D__ANDROID_API__=$API"
INCLUDES="-Iinclude -I$GLUE"

"$CC" -c "$GLUE/android_native_app_glue.c" -o "$B/obj/native_app_glue.o" $INCLUDES $FLAGS
"$TOOLCHAIN/bin/llvm-ar" rcs "lib/$ABI/libnative_app_glue.a" "$B/obj/native_app_glue.o"
"$CXX" -c "$SRC" -o "$B/obj/main.o" -std=c++17 $INCLUDES $FLAGS

# link with the clang++ driver so libc++ comes along automatically
"$CXX" -shared -o "$B/lib/$ABI/libmain.so" "$B/obj/main.o" \
  -L"lib/$ABI" -Wl,-u,ANativeActivity_onCreate -Wl,-z,noexecstack \
  -lraylib -lnative_app_glue -llog -landroid -lEGL -lGLESv2 -lOpenSLES -latomic -lm -ldl \
  -static-libstdc++

# ---------- package APK
mkdir -p "$B/classes"; rm -rf "$B/classes"/*
"$BT/aapt" package -f -m -S "$B/res" -J "$B/src" -M "$B/AndroidManifest.xml" -I "$JAR"
javac -source 1.8 -target 1.8 -bootclasspath "$JAR" -classpath "$JAR" -d "$B/classes" \
  "$B/src/$PKG_PATH/R.java" "$B/src/$PKG_PATH/NativeLoader.java"
"$BT/dx" --dex --output="$B/dex/classes.dex" "$B/classes"

rm -f "$APP.unsigned.apk" "$APP.aligned.apk" "$APP.apk"
"$BT/aapt" package -f -M "$B/AndroidManifest.xml" -S "$B/res" -I "$JAR" \
  -F "$APP.unsigned.apk" "$B/dex"
( cd "$B" && "$BT/aapt" add "$ROOT/$APP.unsigned.apk" "lib/$ABI/libmain.so" )
"$BT/zipalign" -f 4 "$APP.unsigned.apk" "$APP.aligned.apk"
"$BT/apksigner" sign --ks android/raylib.keystore --ks-pass "pass:$STOREPASS" \
  --out "$APP.apk" "$APP.aligned.apk"
rm -f "$APP.unsigned.apk" "$APP.aligned.apk"

echo
echo "Done: $ROOT/$APP.apk"
ls -lh "$APP.apk"