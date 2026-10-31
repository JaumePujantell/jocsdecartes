preset := "default"

build_dir := if preset == "release" { "build-release" } else { "build" }
config := if preset == "release" { "Release" } else { "Debug" }

executable := build_dir / config / "cardgame"

# List the available recipes
default:
    @just --list

# CMake configure
configure:
    cmake --preset {{preset}}

# Build for Linux
build-linux:
    [ -f "{{build_dir}}/CMakeCache.txt" ] || cmake --preset {{preset}}
    cmake --build --preset {{preset}}

# Build and run for Linux
run-linux: build-linux
    "{{executable}}"

alias build := build-linux
alias run := run-linux

# Delete the compiled files
clean:
    [ ! -f "{{build_dir}}/CMakeCache.txt" ] || cmake --build --preset {{preset}} --target clean

# Delete all build directories
distclean: clean-android
    rm -rf build build-release

# Build for Android
build-android:
    cd android-project && ./gradlew assemble{{config}}

# Install APK on connected device
install-android:
    cd android-project && ./gradlew install{{config}}

# Delete the Android build files
clean-android:
    cd android-project && ./gradlew clean

# Format project files in-place
format:
    clang-format -i src/*.c src/*.h

# Run clang-tidy on the project files
tidy:
    [ -f build/compile_commands.json ] || cmake --preset default
    clang-tidy -p build src/*.c src/*.h

# Apply the clang-tidy fixes to the project files
tidy-fix:
    [ -f build/compile_commands.json ] || cmake --preset default
    clang-tidy -p build --fix --fix-errors src/*.c src/*.h
