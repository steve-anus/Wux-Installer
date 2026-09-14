# Wux Installer

A homebrew app for the Wii U that installs a game from a `.wux` disc image on
your SD card. It also installs normal `.app` (WUP) folders you put on the card
yourself. Titles are written to NAND or to a USB drive.

It is based on [Fangal-Airbag]((https://github.com/Fangal-airbag)'s .wuhb port of [Wup Installer GX2](https://github.com/Fangal-Airbag/wup-installer-gx2) and was created with the help of [Maschell](https://github.com/Maschell)'s [Jnuslib](https://github.com/Maschell/JNUSLib).

## Get the app

Download the `.wuhb` from the Releases page of this repository and add it to
your SD card the way your launcher documents. Then start "Wux Installer".

## Put the files on the SD card

Create a folder named `wudump` in the root of the SD card and put three files
in it:

   `yourgame.wux` : the game dump; only the first `.wux` it finds is used, so keep one game at a time in there 
   `game.key` : the disc key for that game, 16 bytes raw, or 32 hex characters 
   `common.key` : your console's common key, the same for every game. 16 bytes raw, or 32 hex characters 

`wudump` is the only place it looks for these. The app also uses a folder named
`install` in the card root for the files it extracts, and creates it if missing.

## How to install

1. Press `install wux` with `A` button
2. Wait
3. Install to NAND or USB
4. Delete files after installation
5. Wait more
6. Done

## Build from source

Needs Linux with devkitPro installed. Besides devkitPPC and `wut`, the link line
uses these devkitPro packages:

```
ppc-zlib ppc-libpng ppc-libjpeg-turbo ppc-libgd ppc-freetype ppc-glm
ppc-brotli ppc-bzip2 ppc-libmad ppc-libogg ppc-libvorbisidec
```

Then build with:

```sh
export DEVKITPRO=/opt/devkitpro   # wherever your devkitPro install is
make
```

The result is `wux_installer.wuhb`, along with `.rpx` and `.elf` files next to
it. `make clean` removes build output.

## Credits

A big thanks goes out to [brienj](https://github.com/xhp-creations) for creating the original rpx port of WUP Installer GX2, [Gary](https://github.com/GaryOderNichts) for making the Wii U controller mod version, [Yardape8000](https://github.com/Yardape8000) for creating the wup installer y mod which WUP Installer GX2 was based on, [His repo](https://github.com/Yardape8000/wupinstaller), [dimok](https://github.com/dimok789) who created the original apps HBL and Loadiine which use the same base, 
as well as the original wupinstaller, [Maschell](https://github.com/Maschell) for creating the Aroma environment and jnuslib.

Also thanks to [Fangal-airbag](https://github.com/Fangal-airbag) for the .wuhb port.

Current maintenance by [steve-anus](https://github.com/steve-anus).
