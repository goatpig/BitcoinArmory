# Armory on macOS

Armory 0.97 runs on macOS from a source build. It was last tested on macOS 27.2 on Apple Silicon. The build instructions take Homebrew's location from `brew --prefix`, so they should also work on Intel Macs (where Homebrew lives in `/usr/local` instead of `/opt/homebrew`), which have not been tested.

## Requirements

- [Homebrew](https://brew.sh). Check with `brew --version`. If the command is not found, install Homebrew with the command from its website:
  ```
  /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
  ```
  When it finishes, run the commands it lists under "Next steps", which add Homebrew to your `PATH`.
- [Bitcoin Core](https://bitcoincore.org): Armory relies on it to connect to the Bitcoin network.

## Building and running Armory

Follow the [macOS build instructions](build_instructions/osx/macos.md). They cover the Homebrew packages Armory needs, the dependencies built from source, the Python environment, and how to start ArmoryQt.

## Reporting problems

Report macOS problems in the [GitHub issues](https://github.com/goatpig/BitcoinArmory/issues) or on the [Armory board](https://bitcointalk.org/index.php?board=97.0) of the Bitcoin Forum.
