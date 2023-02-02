//[of]:comments
/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.
  
  https://github.com/libfuse/libfuse
  
  git clone https://github.com/libfuse/libfuse.git
  sudo apt-get install meson ninja-build pkg-config python3-pip
  
  sudo pip3 install pytest
  mkdir build; cd build
  meson ..
  ninja
  sudo python3 -m pytest test/
  sudo ninja install



*/

/** @file
 *
 * minimal example filesystem using high-level API
 *
 * Compile with:
 *
 *     gcc -Wall blockfs.c `pkg-config fuse3 --cflags --libs` -o blockfs
 *
 * ## Source code ##
 * \include blockfs.c
 */
//[cf]
//[of]:includes/defines
#define FUSE_USE_VERSION 31

#define CHUNKSIZE 1024 * 1024 * 5
#define BLOCKSIZE 1024 * 1024 * 1024 * 1024 * 4
#define MOUNTSPLITS 6

#ifdef linux
/* For pread()/pwrite()/utimensat() */
#define _XOPEN_SOURCE 700
#endif

#define SHOW_MESSAGES_READ_AND_SKIPS 0
#define SHOW_MESSAGES_WRITES 0
#define SHOW_MESSAGES_COPY_STATUS_BAR 0

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>

#include <stdlib.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/param.h>

#include <sys/time.h>

#include <stdarg.h>
//[cf]
//[of]:global vars declaration
char gl_debugMessageCurrent[300];
char gl_debugMessageLast[300];
//[cf]
//[of]:command line options
/*
 * Command line options
 *
 * We can't set default values for the char* fields here because
 * fuse_opt_parse would attempt to free() them when the user specifies
 * different values on the command line.
 */
//[of]:static struct options {
static struct options {
	const char *local_store;
	const char *remote_store;
	const char *blockfile_size;
	const char *chunk_size;
	const char *mount_splits;
	int show_help;
} options;
//[cf]
#define OPTION(t, p)                           \
    { t, offsetof(struct options, p), 1 }
//[of]:static const struct fuse_opt option_spec[] = {
static const struct fuse_opt option_spec[] = {
	OPTION("--local=%s", local_store),
	OPTION("--remote=%s", remote_store),
	OPTION("--blockfile-size=%s", blockfile_size),
	OPTION("--chunk-size=%s", chunk_size),
	OPTION("--mount-splits=%s", mount_splits),
	OPTION("-h", show_help),
	OPTION("--help", show_help),
	FUSE_OPT_END
};
//[cf]
//[of]:static void show_help(const char *progname)
static void show_help(const char *progname)
{
	printf("usage: %s [options] <mountpoint>\n\n", progname);
	printf("File-system specific options:\n"
	       "    --local=<s>            Name of the \"local\" backingstore mountpoint\n"
	       "                             (default: \"/mnt/localfs/\")\n"
	       "    --remote=<s>           Name of the \"remote\" backingstore mountpoint\n"
	       "                             (default: \"/mnt/remotefs/\")\n"
	       "    --blockfile-size=<s>   Size of the \"block\" file in megs\n"
	       "                             (default: \"2048\")\n"
	       "    --chunk-size=<s>       Size of the \"chunk\" files in megs\n"
	       "                             (default: \"5\")\n"
	       "    --mount-splits=<s>     Size of the mount splits.\n"
	       "                           Used for evenly spreading chunks across multiple mounts\n"
	       "                             (default: \"6\")\n"
	       "\n");
}
//[cf]
//[cf]
//[of]:local methods
//[of]:static void _mkdir(const char *dir) {
static void _mkdir(const char *dir) {
    char tmp[256];
    char *p = NULL;
    size_t len;

    int e;
    struct stat sb;


    snprintf(tmp, sizeof(tmp),"%s",dir);
    len = strlen(tmp);
    if (tmp[len - 1] == '/')
        tmp[len - 1] = 0;
    for (p = tmp + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            e = stat(tmp, &sb);
            if (e == 0) {
              if (sb.st_mode & S_IFREG)
                exit (2);
            } else {
              if (errno = ENOENT) {
//[c]                printft("dirname %s",tmp);
                e = mkdir(tmp, S_IRWXU);
                if (e != 0) {
                  exit (3);
                }
              }
            }
            *p = '/';
        }
    mkdir(tmp, S_IRWXU);
}
//[cf]
//[of]:long long round_closest(long long dividend, unsigned int divisor)
long long round_closest(long long dividend, unsigned int divisor)
{
    return (dividend + (divisor / 2)) / divisor;
}
//[cf]
//[of]:int cp(const char *to, const char *from)
int cp(const char *from, const char *to)
{
    int   c;
    FILE *stream_R;
    FILE *stream_W; 

    stream_R = fopen (from, "rb");
    if (stream_R == NULL)
        return -1;

    stream_W = fopen (to, "wb");
    if (stream_W == NULL)
     {
        fclose (stream_R);
        return -2;
     }

#if SHOW_MESSAGES_COPY_STATUS_BAR == 1
    long int n = 0;
    printf("|");
    while ((c = fgetc(stream_R)) != EOF) {
      if (n % ((long int) 1024 * 1024 * atoi(options.chunk_size) / 20) + 1 == ((long int) 1024 * 1024 * atoi(options.chunk_size) / 20))
        printf("-");
      fputc (c, stream_W);
      n += 1;
    }
    printf("|\n");
#else
    while ((c = fgetc(stream_R)) != EOF) {
      fputc (c, stream_W);
    }
#endif

    fclose (stream_R);
    fclose (stream_W);

    return 0;
}
//[cf]
//[of]:void printft( const char* string, ... ) {
void printft( const char* string, ... ) {
  time_t rawtime = time(NULL);
  struct tm *ptm = localtime(&rawtime);

  struct timeval  tv;
  gettimeofday(&tv, NULL);
  double time_in_mill = (tv.tv_usec) / 1000 ; // convert tv_sec & tv_usec to millisecond

  va_list args;
  printf("%02d:%02d:%02d:%03d -- ", ptm->tm_hour, ptm->tm_min, ptm->tm_sec, (int) time_in_mill);
  va_start( args, string );
  vprintf( string, args );
  va_end( args );
//[c]  printf( "\n" );
}
//[cf]
//[cf]
//[of]:block methods
//[of]:static int blockfs_readblocks(char *buf, size_t size, off_t offset)
//[c]if chunk file not exist local
//[c]  if chunk file exist remote
//[c]    pull compressed/encrypted file
//[c]    decrypt chunk
//[c]    decompress chunk
//[c]  else
//[c]    return 0s in buffer
//[c]read blocks from chunk
//[c]

extern int errno;

static int blockfs_readblocks(char *buf, size_t size, off_t offset)
{
//[of]:  setup vars
	int res;

  long int chunkSize = (long int) 1024 * 1024 * atoi(options.chunk_size);
  long int chunkOffset;
  unsigned int chunkId;
  long int driveIndex;

	size_t bufferOffset = 0;
	size_t bufferRemaining = size;
	size_t readLength;
	
	size_t filesize;

  char chunkIdText[12];
  char driveIndexText[4];
  char dirLevel1[3];
  char dirLevel2[3];
  char dirLevel3[3];
  char dirLevel4[3];
  char filePathSuffix[9];
  char fileName[7];
  char filePathLocal[256];
  char fileNameLocal[256];
  char fileNameRemote[256];

  FILE *file_ptr = NULL;
//[cf]
//[of]:	while (bufferRemaining > 0) {
	while (bufferRemaining > 0) {
  	chunkId = (offset + bufferOffset) / chunkSize;
  	chunkOffset = (offset + bufferOffset) % chunkSize;
  	driveIndex = chunkId % atoi(options.mount_splits) + 1;
    
//[of]:    encode chunk filename
    sprintf(chunkIdText, "%08d", chunkId);
    sprintf(driveIndexText, "%01ld", driveIndex);

    memcpy( dirLevel1, &chunkIdText[0], 2 );
    dirLevel1[2] = '\0';
    memcpy( dirLevel2, &chunkIdText[2], 2 );
    dirLevel2[2] = '\0';
    memcpy( dirLevel3, &chunkIdText[4], 2 );
    dirLevel3[2] = '\0';
    memcpy( dirLevel4, &chunkIdText[6], 2 );
    dirLevel4[2] = '\0';

    strcpy(fileName, dirLevel4);
    strcat(fileName, ".dat");

    strcpy(filePathSuffix, dirLevel1);
    strcat(filePathSuffix, "/");
    strcat(filePathSuffix, dirLevel2);
    strcat(filePathSuffix, "/");
    strcat(filePathSuffix, dirLevel3);

    strcpy(fileNameLocal, options.local_store);
    strcat(fileNameLocal, driveIndexText);
    strcat(fileNameLocal, "/");
    strcat(fileNameLocal, filePathSuffix);
    strcpy(filePathLocal, fileNameLocal);
    strcat(fileNameLocal, "/");
    strcat(fileNameLocal, fileName);
//[cf]

		readLength = MIN(chunkSize - chunkOffset, bufferRemaining);

    memset(buf + bufferOffset, 0, readLength);

//[of]:    #if SHOW_MESSAGES_READ_AND_SKIPS == 1
    #if SHOW_MESSAGES_READ_AND_SKIPS == 1
      sprintf(gl_debugMessageCurrent, "reading ----------- %s", fileNameLocal);
      if (strcmp(gl_debugMessageCurrent, gl_debugMessageLast) != 0) {
        printft("%s\n", gl_debugMessageCurrent);
        strcpy(gl_debugMessageLast,gl_debugMessageCurrent);
      }
    #endif
//[cf]

//[of]:    if local file does not exist, try to get remote
    if( access( fileNameLocal, F_OK ) == -1 ) {
//[c]      printf("local file %s not found\n",fileNameLocal);
      // local file not exists

      strcpy(fileNameRemote, options.remote_store);
      strcat(fileNameRemote, driveIndexText);
      strcat(fileNameRemote, "/");
      strcat(fileNameRemote, filePathSuffix);
      strcat(fileNameRemote, "/");
      strcat(fileNameRemote, fileName);

      if( access( fileNameRemote, F_OK ) != -1 ) {
        printft("r copying %s to %s\n",fileNameRemote, fileNameLocal);

        //mkdir path if it doesn't exist
        _mkdir(filePathLocal);

        res = cp(fileNameRemote, fileNameLocal);
        if (res != 0) {
          res = -errno;
          return res;
        }
        printft("r copying complete\n");
//[c]      } else {
//[c]        printf("remote file %s not found\n",fileNameRemote);
      }
    }
//[cf]
//[of]:    if local file exist, read it
    if( access( fileNameLocal, F_OK ) != -1 ) {
      // file exists

      file_ptr = fopen(fileNameLocal,"rb");  // r open for reading, b for binary

      //get size of file
      fseek(file_ptr, 0L, SEEK_END);
      filesize = ftell(file_ptr);
      fseek(file_ptr, 0L, SEEK_SET);

//[of]:      if (chunkOffset < filesize) {
      if (chunkOffset < filesize) {
        readLength = MIN(filesize - chunkOffset, readLength);
        fseek(file_ptr, chunkOffset, SEEK_SET);
        res = fread(buf + bufferOffset, readLength, 1, file_ptr); // read bytes to buffer
      }
//[cf]

      fclose(file_ptr);
    	if (res == -1) {
    		res = -errno;
        return res;
      }
    }
//[cf]
    
		bufferRemaining -= readLength;
		bufferOffset += readLength;
		if (bufferRemaining == 0)
			return size;
	}
//[cf]
	return 0;
}
//[cf]
//[of]:static int blockfs_writeblocks(const char *buf, size_t size, off_t offset)
static int blockfs_writeblocks(const char *buf, size_t size, off_t offset)
{
//[of]:  setup vars
  int res;

  long int chunkSize = (long int) 1024 * 1024 * atoi(options.chunk_size);
  long int chunkOffset;
  unsigned int chunkId;
  long int driveIndex;

	size_t bufferOffset = 0;
	size_t bufferRemaining = size;
	size_t writeLength;



  char chunkIdText[12];
  char driveIndexText[4];
  char dirLevel1[3];
  char dirLevel2[3];
  char dirLevel3[3];
  char dirLevel4[3];
  char filePathSuffix[9];
  char fileName[7];
  char filePathLocal[256];
  char fileNameLocal[256];
  char fileNameRemote[256];

  FILE *file_ptr = NULL;
//[cf]
//[of]:	while (bufferRemaining > 0) {
	while (bufferRemaining > 0) {
  	chunkId = (offset + bufferOffset) / chunkSize;
  	chunkOffset = (offset + bufferOffset) % chunkSize;
  	driveIndex = chunkId % atoi(options.mount_splits) + 1;
  	
//[of]:    encode chunk filename
    sprintf(chunkIdText, "%08d", chunkId);
    sprintf(driveIndexText, "%01ld", driveIndex);

    memcpy( dirLevel1, &chunkIdText[0], 2 );
    dirLevel1[2] = '\0';
    memcpy( dirLevel2, &chunkIdText[2], 2 );
    dirLevel2[2] = '\0';
    memcpy( dirLevel3, &chunkIdText[4], 2 );
    dirLevel3[2] = '\0';
    memcpy( dirLevel4, &chunkIdText[6], 2 );
    dirLevel4[2] = '\0';

    strcpy(fileName, dirLevel4);
    strcat(fileName, ".dat");

    strcpy(filePathSuffix, dirLevel1);
    strcat(filePathSuffix, "/");
    strcat(filePathSuffix, dirLevel2);
    strcat(filePathSuffix, "/");
    strcat(filePathSuffix, dirLevel3);

    strcpy(fileNameLocal, options.local_store);
    strcat(fileNameLocal, driveIndexText);
    strcat(fileNameLocal, "/");
    strcat(fileNameLocal, filePathSuffix);
    strcpy(filePathLocal, fileNameLocal);
    strcat(fileNameLocal, "/");
    strcat(fileNameLocal, fileName);
//[cf]

//[of]:    if local file does not exist, try to get remote or create
    if( access( fileNameLocal, F_OK ) == -1 ) {
      // local file not exists

      //mkdir path if it doesn't exist
      _mkdir(filePathLocal);

      strcpy(fileNameRemote, options.remote_store);
      strcat(fileNameRemote, driveIndexText);
      strcat(fileNameRemote, "/");
      strcat(fileNameRemote, filePathSuffix);
      strcat(fileNameRemote, "/");
      strcat(fileNameRemote, fileName);

      if( access( fileNameRemote, F_OK ) != -1 ) {
        printft("w copying %s to %s\n",fileNameRemote, fileNameLocal);

        res = cp(fileNameRemote, fileNameLocal);
        if (res != 0) {
          res = -errno;
          return res;
        }
        printft("w copying complete\n");
      } else {
        //create flie if it doesn't exist
        printft("w creating %s\n", fileNameLocal);

        file_ptr = fopen(fileNameLocal,"w");
        fclose(file_ptr);
      }
    }
//[cf]

		writeLength = MIN(chunkSize - chunkOffset, bufferRemaining);

//[of]:    #if SHOW_MESSAGES_WRITES == 1
    #if SHOW_MESSAGES_WRITES == 1
      sprintf(gl_debugMessageCurrent, "writing ----------- %s", fileNameLocal);
      if (strcmp(gl_debugMessageCurrent, gl_debugMessageLast) != 0) {
        printft("%s\n", gl_debugMessageCurrent);
        strcpy(gl_debugMessageLast,gl_debugMessageCurrent);
      }
    #endif
//[cf]

    file_ptr = fopen(fileNameLocal,"r+b");  // r+ open for update and writing, b for binary
    fseek(file_ptr, chunkOffset, SEEK_SET);
    fwrite(buf + bufferOffset, writeLength, 1, file_ptr); // write bytes from buffer
    fclose(file_ptr);

		bufferRemaining -= writeLength;
		bufferOffset += writeLength;
		if (bufferRemaining == 0)
			return size;
	}
//[cf]
	return 0;
}
//[cf]
//[cf]
//[of]:mounted filesystem methods
//[of]:static int blockfs_getattr(const char *path, struct stat *stbuf,
static int blockfs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
	(void) fi;
	int res = 0;

	memset(stbuf, 0, sizeof(struct stat));
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} else if (strcmp(path+1, "block") == 0) {
		stbuf->st_mode = S_IFREG | 0666;
		stbuf->st_nlink = 1;
		stbuf->st_size = (long long) 1024 * 1024 * atol(options.blockfile_size);
	} else
		res = -ENOENT;

	return res;
}
//[cf]
//[of]:static int blockfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
static int blockfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags)
{
	(void) offset;
	(void) fi;
	(void) flags;

	if (strcmp(path, "/") != 0)
		return -ENOENT;

	filler(buf, ".", NULL, 0, 0);
	filler(buf, "..", NULL, 0, 0);
	filler(buf, "block", NULL, 0, 0);

	return 0;
}
//[cf]
//[c]
//[of]:file methods
//[of]:static int blockfs_open(const char *path, struct fuse_file_info *fi)
static int blockfs_open(const char *path, struct fuse_file_info *fi)
{
	if (strcmp(path+1, "block") != 0)
		return -ENOENT;

	return 0;
}
//[cf]
//[of]:static int blockfs_read(const char *path, char *buf, size_t size, off_t offset,
static int blockfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	(void) fi;
	if(strcmp(path+1, "block") != 0)
		return -ENOENT;

	size = blockfs_readblocks(buf, size, offset);

	return size;
}
//[cf]
//[of]:static int blockfs_write(const char *path, const char *buf, size_t size,
static int blockfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	(void) fi;
	if(strcmp(path+1, "block") != 0)
		return -ENOENT;

	size = blockfs_writeblocks(buf, size, offset);

	return size;
}
//[cf]
//[of]:static int blockfs_fsync(const char *path, int isdatasync,
static int blockfs_fsync(const char *path, int isdatasync,
		     struct fuse_file_info *fi)
{
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */

	(void) path;
	(void) isdatasync;
	(void) fi;
	return 0;
}
//[cf]
//[cf]
//[cf]
//[of]:main
//[of]:static void *blockfs_init(struct fuse_conn_info *conn,
static void *blockfs_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
	(void) conn;
	cfg->use_ino = 1;

	/* Pick up changes from lower filesystem right away. This is
	   also necessary for better hardlink support. When the kernel
	   calls the unlink() handler, it does not know the inode of
	   the to-be-removed entry and can therefore not invalidate
	   the cache of the associated inode - resulting in an
	   incorrect st_nlink value being reported for any remaining
	   hardlinks to this inode. */
	cfg->entry_timeout = 0;
	cfg->attr_timeout = 0;
	cfg->negative_timeout = 0;

	return NULL;
}
//[cf]
//[of]:static struct fuse_operations blockfs_oper = {
static struct fuse_operations blockfs_oper = {
	.init     = blockfs_init,
	.getattr	= blockfs_getattr,
	.readdir	= blockfs_readdir,
	.open     = blockfs_open,
	.read     = blockfs_read,
	.write		= blockfs_write,
	.fsync		= blockfs_fsync,
};
//[cf]
//[of]:int main(int argc, char *argv[])
int main(int argc, char *argv[])
{
	int ret;
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	/* Set defaults -- we have to use strdup so that
	   fuse_opt_parse can free the defaults if other
	   values are specified */
	options.local_store = strdup("/mnt/localfs/");
	options.remote_store = strdup("/mnt/remotefs/");
	options.blockfile_size = strdup("2048");
	options.chunk_size = strdup("5");
	options.mount_splits = strdup("6");

	/* Parse options */
	if (fuse_opt_parse(&args, &options, option_spec, NULL) == -1)
		return 1;

  // enable single thread operation cuz this doesn't work with multi-threaded
  fuse_opt_add_arg(&args, "-s");

  printf("and we're off!\n");

	/* When --help is specified, first print our own file-system
	   specific help text, then signal fuse_main to show
	   additional help (by adding `--help` to the options again)
	   without usage: line (by setting argv[0] to the empty
	   string) */
	if (options.show_help) {
		show_help(argv[0]);
		assert(fuse_opt_add_arg(&args, "--help") == 0);
		args.argv[0][0] = '\0';
	}

	ret = fuse_main(args.argc, args.argv, &blockfs_oper, NULL);
	fuse_opt_free_args(&args);
	return ret;
}
//[cf]
//[cf]
