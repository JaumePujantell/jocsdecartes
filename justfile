build-linux:
    cmake --preset default
    cmake --build --preset default

run-linux: build-linux
    ./build/cardgame

alias build := build-linux
alias run := run-linux

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
