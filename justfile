default: build

# Build for Linux (native)
build:
    cmake --preset default
    cmake --build --preset default

# Build for Windows using MinGW cross-compiler
build-windows:
    cmake -B build-windows \
        -DCMAKE_TOOLCHAIN_FILE=/usr/share/mingw/toolchain-x86_64-w64-mingw32.cmake \
        -DCMAKE_BUILD_TYPE=Release
    cmake --build build-windows --parallel

# Build for Android (debug APK)
build-android:
    cd android-project && ./gradlew assembleDebug

# Build for Android (release APK)
build-android-release:
    cd android-project && ./gradlew assembleRelease

# Install debug APK on connected device
install-android:
    cd android-project && ./gradlew installDebug

# Remove all build directories
clean:
    rm -rf build build-windows
    cd android-project && ./gradlew clean
