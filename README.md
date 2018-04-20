# edd

## Overview

This is extremely raw code I put together to solve a specific problem.
Configured and operated by editing main() and recompiling.  It might be
turned into a utility eventually but is being put out there just in case
the code is of use to someone somehow sooner than that "someday".

A client gave me a failing SATA hard drive from a Windows 10 system.  It was
partially readable with GNU ddrescue, but periodically froze and required a
hardware reset, so only about a third of the drive could be read
"conveniently".

My go-to tools for NTFS forensics (The Sleuth Kit) needed a healthier
filesystem to operate correctly.  For example, the gaps in my partial
ddrescue image meant that TSK could "recover" a file with holes in it -- TSK
has no way of being aware of the empty regions from the ddrescue mapfile.
So I figured on trying to build some sort of bridge.

This code was cobbled together to do the following:

1. Read as much of the directory structure as possible, both to manually look
   over the filesystem for the files of interest and to identify the clusters
   of the hard drive that were actually in use.
1. Maximally rely on the partial ddrescue image, but allow some form of
   identification and recovery of missing clusters for the files of interest.
   The recovery goes into a not-particularly-efficient overlay system that
   allows the recovered clusters to be read in preference to those in the
   partial ddrescue image.  (I didn't want to alter the ddrescue image.)
1. Attempt to read the SATA drive using SG_IO ioctl.  I never got consistent
   results from this code (reader.c and reader.h) though it felt... almost
   there?  Don't use it.
1. Restore files, eventually recursively, identifying when clusters that
   are necessary to restore a file are missing.
1. Develop a better understanding of NTFS.

What's here is a quick-and-dirty hack that was just enough for me to get
the important data off this particular drive.  It's not robust.  It's not
clean.  It won't handle anything but the most basic NTFS (encryption?
compression? nope.)  And it leaks memory.

But it was fun to write and got the job done!
