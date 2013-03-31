# MultiROM reference installer
This is a reference installer file structure.
It should be used only for Linux
based ROMs, where classic update.zip format is unsuitable.

## Installation file
The installation file itself is a ZIP archive, renamed to `*.mrom` so that
recovery can know what is just ZIP archive and what is MultiROM
installer
file.
I recommend not to use compression when making this ZIP, the installation
will be faster and the ROM is already compressed in .tar.gz files.

While this format should be versatile enough, feel free to contact me if you
need something changed - if it is reasonable, there will be no problem
adding changes you need.

### Content
* **manifest.txt** - File with info for the recovery. Read the comments in that
                     file to know more.

* **rom** - Folder with tar.gz archives containing each of the ROM base folders
            (e.g. `root.tar.gz`, `system.tar.gz`, ...). These can be split to
            multiple files (and should be, if the file is bigger than ~800 MB).
            Pattern is name_XX.tar.gz, so for example `root_00.tar.gz` and
            `root_01.tar.gz`. Numbering __must__ start at 00!
            Command `tar --numeric-owner --overwrite -xf` is used to extract
            these tar files.

* **root_dir** - Content of this folder will be copied to root of the ROM
                 folder - `/sdcard/multirom/roms/*rom_name*`. It can contain
                 `rom_info.txt` if it's Linux ROM or the `boot` folder and/or
                 `boot.img` if it's Android-based ROM.

* **pre_install, post_install** - Sh scripts in these folders will be ran
                before/after the installation. They must return success return
                code else the installation is aborted. Path to root
                folder/folder where images are mounted is
                passed as argument to this script, script can then cd to one of
                the base folders and do whatever it wants to. Scripts are ran
                in alphabetical order (use numbers, `01_script.sh`, `02_script.sh`).
                **All** files from both directories are extracted to `/tmp/script/`,
                which means you can put e.g. binary blobs in there and copy them
                to proper place in the sh scripts or pack some binaries needed
                by the scripts (e.g. gnutar, remember to set chmod before running them).
