# BMD VideoHub Simulator

Simulator for Blackmagic Design VideoHubs (for development of custom client software without access to VideoHub hardware).

When cloning this repository, you make wish to use the following git command to automatically initialize the included submodules:

    git clone --recurse-submodules -j8 <repopath>

## Requirements

You need Qt to build this project.

If you are on a mac you can use Homebrew to install it:

    # install the files
    brew install qt5
    # for adding files to your paths
    # Otherwise, you can add the PATH output from the install command
    # to your PATH so that you can find qmake
    brew link qt5 --force

Additionally, this project uses a git submodule for QtZeroConf. After cloning this repository, initialize it by running:

    git submodule init
    git submodule update

## Building

    cd source
    qmake -makefile
    make

This will output an executable named "BmdVideoHub". Run it with "./BmdVideoHub".
