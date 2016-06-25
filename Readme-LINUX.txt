PCem v8.1 Linux supplement


You will need the following libraries :

Allegro 4.x
OpenAL
ALut

and their dependencies.

Open a terminal window, navigate to the PCem directory then enter

./configure
make

then ./pcem to run.


The Linux port is currently entirely unpolished, and mainly exists as a starting point for 
anyone who wants to make a better port.

The menu is not available all the time. Press CTRL-ALT-PGDN to open it.

The mouse does not work very well, at least on my machine. This is most likely an Allegro issue.

Fullscreen mode is not present.

Video acceleration is not used at all, so performance is inferior to the Windows version.

CD-ROM support currently only accesses /dev/cdrom. It has not been heavily tested.
