#!/bin/bash
PACKAGE_FOLDER=$1
BRANCH=$2
LIBRARY_NAME="pcsx_rearmed_libretro_switch.a"
NRO_NAME="pcsx_rearmed.nro"
APP_TITLE="PCSX ReARMed"

mkdir $PACKAGE_FOLDER
make platform=switch -j10 -f Makefile.libretro
git clone -b $BRANCH https://git.m4xw.net/Switch/RetroArch/RetroArch.git --depth=1
cp $LIBRARY_NAME RetroArch/libretro_switch.a
cp assets/icon.jpg RetroArch/icon.jpg
cd RetroArch/
mkdir romfs
APP_TITLE="$APP_TITLE" make -f Makefile.switch -j10
cp retroarch_switch.nro ../$PACKAGE_FOLDER/$NRO_NAME
