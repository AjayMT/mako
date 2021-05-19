
# Mako
Mako is an operating system for 32-bit x86-compatible computers. Among its features are:
- Linux-compatible ext2 and USTAR filesystems
- fully virtualized per-process address spaces
- pipes and signals for inter-process communication
- cooperative and pre-emptive multitasking -- multiple processes and multiple threads per process
- a graphical user interface
- a (mostly) UNIX-compatible C library
- a Lua interpreter and a port of DOOM
- graphical applications for navigating directories, editing files and executing programs

![](http://ajaymt.github.io/mako/res/screenshot.png)

Mako is named after the [mako shark](https://marinebio.org/species/shortfin-mako-sharks/isurus-oxyrinchus/), the fastest shark in the sea. The shortfin mako shark is classified as an endangered species by the [IUCN](http://www.iucn.org) -- learn more about shark conservation [here](https://www.sharktrust.org/shark-conservation).

'Mako' is also the name of the fictional source of energy from [Final Fantasy VII](https://finalfantasy.fandom.com/wiki/Final_Fantasy_VII).

## Download
Download `mako.iso` and `mako-hda.img` from [here](https://github.com/AjayMT/mako/tree/release).

## Build from source
This build process has been tested on macOS. It *should* work on most Linux-like platforms; if you are having trouble building Mako, please reach out to me or raise an issue on this repository.

Steps:
1. Build or acquire a [cross-compiling GCC toolchain](https://wiki.osdev.org/GCC_Cross-Compiler) that targets the `i386-elf` platform. This is the hardest and most time consuming step -- if you can find precompiled binaries of `i386-elf-gcc` and binutils for your platform, save yourself the effort and use them instead of building GCC from source. After this is complete, you should have `i386-elf` versions of GCC and binutils:
```sh
$ i386-elf-gcc --version
i386-elf-gcc (GCC) 9.2.0
Copyright (C) 2019 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

$ i386-elf-ld --version
GNU ld (GNU Binutils) 2.31
Copyright (C) 2018 Free Software Foundation, Inc.
This program is free software; you may redistribute it under the terms of
the GNU General Public License version 3 or (at your option) a later version.
This program has absolutely no warranty.
```
2. Install `xorriso` and build/acquire the `i386-elf` version of `grub-mkrescue`: <https://wiki.osdev.org/GRUB#Installing_GRUB_2_on_OS_X> (these instructions are for macOS but AFAIK they should work on most platforms).
3. Clone the Mako repository and run the following commands:
```sh
./fetch-deps.sh
make user # Ignore warnings
./gen-hda.sh # Ignore the segfault message
make
```
4. You should now have the `mako.iso` and `hda.img` disk images!

## Run it
Mako only works on [qemu](https://www.qemu.org/) at the moment.

1. Install qemu.
2. Download the `mako.iso` and `mako-hda.img` disk images from the link above.

```sh
# At least 64M of RAM is recommended
qemu-system-i386 -cdrom mako.iso -m 256M -drive format=raw,file=mako-hda.img
```

## Roadmap
TODOs:
- More+better documentation.
- QOL improvements.
- Many small misc. things in the code.

Long term goals:
- Full POSIX compliant libc.
- Port a C compiler.
- Network/audio stack.

## Acknowledgements
Mako makes use of the following libraries and programs:
- This excellent `printf` implementation by Marco Paland: <https://github.com/mpaland/printf>
- LodePNG, the small PNG encoder and decoder: <https://lodev.org/lodepng/>
- The [Lua](http://lua.org) programming language
- This very portable version of DOOM: <https://github.com/ozkl/doomgeneric>
- This small C compiler: <https://github.com/rswier/c4>

I wrote Mako to better understand how operating systems work and learnt a lot of cool stuff in the process. This project would not exist without:
- the [osdev wiki](https://wiki.osdev.org/) and the friendly people of the #osdev IRC channel
- [The little book about OS Development](https://littleosbook.github.io/)
- the very well-documented source code of [ToAruOS](http://github.com/klange/toaruos), [SerenityOS](https://github.com/SerenityOS/serenity) and many other hobby-OS projects.

## License
All Mako source is distributed under the terms of the MIT License.
