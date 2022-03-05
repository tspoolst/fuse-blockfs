% BLOCKFS(1) Version 0.0.0 | BlockFS Documentation

BLOCKFS
=======

- [NAME](#name)
- [SYNOPSIS](#synopsis)
- [DESCRIPTION](#description)
- [OPTIONS](#options)
- [EXAMPLES](#examples)
- [SEE ALSO](#see-also)
- [COPYRIGHT](#copyright)

------------------------------------------------------------------------

# NAME
--------------

blockfs-nocopy - presents a single block file that is backed by chunk
files.

# SYNOPSIS
----------------------

**blockfs-nocopy** { **-h** \| **-V** \| **\--local=\<s**\>
**\--blockfile-size=\<s**\> **\--chunk-size=\<s**\>
**\--mount-splits=\<s**\> *\<mountpoint*\> }

# DESCRIPTION
----------------------------

BlockFS - a proper description will to come, eventually.\
  Initial notes:\
    It is very important that one and only one layer of the filesystem
stack makes use of system cache.

# OPTIONS
--------------------

**\--local=\<s**\>

Name of the \"local\" backingstore mountpoint\
(default: \"/mnt/localfs/\")

**\--blockfile-size=\<s**\>

Size of the \"block\" file in megs\
(default: \"2048\")

**\--chunk-size=\<s**\>

Size of the \"chunk\" files in megs\
(default: \"5\")

**\--mount-splits=\<s**\>

Size of the mount splits.\
Used for evenly spreading chunks across multiple mounts\
(default: \"6\")

**-h**

Displays a short usage summary and all the options provided by the FUSE
library.

\

In addition to the options described above, there are quite a few
options provided by the [FUSE]{.small} library. For most of those, I
don't even know what they do and there doesn't seem to be any
documentation. If you run blockfs-nocopy with the **-h** option, you
will get a list of those options.

Note that, by default, the permissions described above are used for
presentation only, not for actual permission checking. You might want to
use **-o default\_permissions** to change that, and maybe **-o
allow\_other** to actually allow others to access your block file.

# EXAMPLES
----------------------

./blockfs-nocopy \\\
  -o allow\_root,sync \\\
  \--blockfile-size=4194304 \\\
  \--chunk-size=5 \\\
  \--mount-splits=6 \\\
  \--local=/mnt/remotefs/m0/ \\\
  /mnt/blockfs/m0/

This gives you a 4TB block file at \"/mnt/blockfs/m0/\" with a backing
store composed of 5MB chunk files split evenly into 6 different base
directories at \"/mnt/remotefs/m0/\".

ls -lh /mnt/blockfs/m0/\
 total 0\
 -rw-rw-rw- 1 root root 4.0T Dec 31 1969 block

(summarized) find /mnt/remotefs/m0 -type f\
 /mnt/remotefs/m0/1/00/00/00/00.dat\
 /mnt/remotefs/m0/1/00/00/00/36.dat\
 /mnt/remotefs/m0/1/00/00/00/72.dat\
 /mnt/remotefs/m0/2/00/00/04/33.dat\
 /mnt/remotefs/m0/2/00/00/04/69.dat\
 /mnt/remotefs/m0/6/00/00/09/35.dat\
 /mnt/remotefs/m0/6/00/00/09/71.dat

ncdu /mnt/remotefs/m0\
 \-\-- /mnt/remotefs/m0 \-\--\
  389.9 GiB \[\#\#\#\#\#\#\#\#\#\#\] /4\
  389.9 GiB \[\#\#\#\#\#\#\#\#\# \] /2\
  389.9 GiB \[\#\#\#\#\#\#\#\#\# \] /3\
  389.9 GiB \[\#\#\#\#\#\#\#\#\# \] /5\
  389.9 GiB \[\#\#\#\#\#\#\#\#\# \] /1\
  389.9 GiB \[\#\#\#\#\#\#\#\#\# \] /6

# SEE ALSO
----------------------

**blockfs-nocopy**(1)

<http://scriptvoodoo.org/>\
<https://github.com/tspoolst/fuse-blockfs>

# COPYRIGHT
------------------------

Copyright (C) 2016-2022 Trent Spoolstra \<tspoolst\@gmail.com\>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

------------------------------------------------------------------------
