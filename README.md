# fuse-blockfs
presents a single large file, to be loop mounted, that is backed by compressed/encrypted 5 meg blocks.

the primary function is to present a raw block file that can be mounted localy.
the backing blocks can be stored on any number of cloud filesystems.

the target filesystem is treated as ro.

all writes are stored in a local cache directory.

currently syncing blocks requires manual copying of block files from cache to target

possible uses include:
  your own private store
  a public base filesystem for running various operating systems or apps.   i.e. running a huge install of debian while only downloading the pieces that actually get used.

# wanted -- a python fuse fs that caches only directory entries.  not the data just the names and attributes.
doing a directory comparison between google drive and a local dir is painfully slow.
