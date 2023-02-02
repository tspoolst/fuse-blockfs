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

#ifdef linux
/* For pread()/pwrite()/utimensat() */
#define _XOPEN_SOURCE 700
#endif

#define SHOW_MESSAGES_MISC 0
#define SHOW_MESSAGES_READ_AND_SKIPS 0
#define SHOW_MESSAGES_WRITES 0
#define SHOW_MESSAGES_SIZES 0
#define USE_NEW_DEV_CODE 0
#define USE_NEW_FILE_HANDLE_CODE 1

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>

#include <stdlib.h>
#include <dirent.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/param.h>

#include <sys/time.h>

#include <stdarg.h>
//[cf]
//[of]:global vars declaration
off_t gl_blockFileSize;
long int gl_chunkSize;
int gl_mountSplits;
char gl_debugMessageCurrent[120];
char gl_debugMessageLast[120];
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
  const char *blockfile_size;
  const char *chunk_size;
  const char *mount_splits;
  int show_help;
} options;
//[cf]
//[of]:static const struct fuse_opt option_spec[] = {
#define OPTION(t, p)                           \
    { t, offsetof(struct options, p), 1 }
static const struct fuse_opt option_spec[] = {
  OPTION("--local=%s", local_store),
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


#if USE_NEW_DEV_CODE == 1
//[of]:int isDirectoryEmpty(char *dirname) {
int isDirectoryEmpty(char *dirname) {
  int n = 0;
  struct dirent *d;
  DIR *dir = opendir(dirname);
  if (dir == NULL) //Not a directory or doesn't exist
    return 1;
  while ((d = readdir(dir)) != NULL) {
    if(++n > 2)
      break;
  }
  closedir(dir);
  if (n <= 2) //Directory Empty
    return 1;
  else
    return 0;
}
//[cf]
//[of]:int remove_directory(const char *path) {
//[c]remove file
//[c]  while current dir is empty
//[c]    rm dir
//[c]    set current dir to parrent dir i.e. chop path string
//[c]  done

int remove_directory(const char *path) {
   DIR *d = opendir(path);
   size_t path_len = strlen(path);
   int r = -1;

   if (d) {
      struct dirent *p;

      r = 0;
      while (!r && (p=readdir(d))) {
          int r2 = -1;
          char *buf;
          size_t len;

          /* Skip the names "." and ".." as we don't want to recurse on them. */
          if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, ".."))
             continue;

          len = path_len + strlen(p->d_name) + 2; 
          buf = malloc(len);

          if (buf) {
             struct stat statbuf;

             snprintf(buf, len, "%s/%s", path, p->d_name);
             if (!stat(buf, &statbuf)) {
                if (S_ISDIR(statbuf.st_mode))
                   r2 = remove_directory(buf);
                else
                   r2 = unlink(buf);
             }
             free(buf);
          }
          r = r2;
      }
      closedir(d);
   }

   if (!r)
      r = rmdir(path);

   return r;
}
//[cf]
#endif



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
//[c]    long int   n;
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

//[c]    n = 0;
//[c]    printf("|");
    while ((c = fgetc(stream_R)) != EOF) {
//[c]      if (n % ((long int) 1024 * 1024 * atoi(options.chunk_size) / 20) + 1 == ((long int) 1024 * 1024 * atoi(options.chunk_size) / 20))
//[c]        printf("-");
      fputc (c, stream_W);
//[c]      n += 1;
    }
//[c]    printf("|\n");

    fclose (stream_R);
    fclose (stream_W);

    return 0;
}
//[cf]
//[of]:int is_empty(char * buf, size_t size) 
int is_empty(const char *buf, size_t size)
{
    return buf[0] == 0 && !memcmp(buf, buf + 1, size - 1);
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
  printf( "\n" );
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
  char fileName[256];


  FILE *file_ptr = NULL;
//[cf]
//[of]:	while (bufferRemaining > 0) {
  while (bufferRemaining > 0) {
    chunkId = (offset + bufferOffset) / gl_chunkSize;
    chunkOffset = (offset + bufferOffset) % gl_chunkSize;
    driveIndex = chunkId % gl_mountSplits + 1;
    
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


    strcpy(fileName, options.local_store);

    strcat(fileName, driveIndexText);

    strcat(fileName, "/");
    strcat(fileName, dirLevel1);

    strcat(fileName, "/");
    strcat(fileName, dirLevel2);

    strcat(fileName, "/");
    strcat(fileName, dirLevel3);


    strcat(fileName, "/");
    strcat(fileName, dirLevel4);
    strcat(fileName, ".dat");
//[cf]

//[c]if filename has not changed
//[c]  no need to close and re-open the same file

    readLength = MIN(gl_chunkSize - chunkOffset, bufferRemaining);

    //fill buffer with all 0s first
    memset(buf + bufferOffset, 0, readLength);

//[of]:    #if SHOW_MESSAGES_READ_AND_SKIPS == 1
    #if SHOW_MESSAGES_READ_AND_SKIPS == 1
      sprintf(gl_debugMessageCurrent, "reading ----------- %s", fileName);
      if (strcmp(gl_debugMessageCurrent, gl_debugMessageLast) != 0) {
        printft("%s", gl_debugMessageCurrent);
        strcpy(gl_debugMessageLast,gl_debugMessageCurrent);
      }
    #endif
//[cf]

#if USE_NEW_FILE_HANDLE_CODE == 1
//[c]the read/write size calls are always 4096 bytes
//[c]  not sure why
//[c]  to do the file open/close reduction of calls the file write/read names and handles need to survive between calls.
    if( access( fileName, F_OK ) != -1 ) {
      // file exists

      file_ptr = fopen(fileName,"rb");  // r open for reading, b for binary

      //get size of file
      fseek(file_ptr, 0L, SEEK_END);
      filesize = ftell(file_ptr);
      fseek(file_ptr, 0L, SEEK_SET);

      if (chunkOffset < filesize) {
        readLength = MIN(filesize - chunkOffset, readLength);
        fseek(file_ptr, chunkOffset, SEEK_SET);
        res = fread(buf + bufferOffset, readLength, 1, file_ptr); // read bytes to buffer
      }

      fclose(file_ptr);
      if (res == -1) {
        res = -errno;
        return res;
      }
    }

    bufferRemaining -= readLength;
    bufferOffset += readLength;
    if (bufferRemaining == 0)
      return size;
#else
    if( access( fileName, F_OK ) != -1 ) {
      // file exists
      
      file_ptr = fopen(fileName,"rb");  // r open for reading, b for binary

      //get size of file
      fseek(file_ptr, 0L, SEEK_END);
      filesize = ftell(file_ptr);
      fseek(file_ptr, 0L, SEEK_SET);

      if (chunkOffset < filesize) {
        readLength = MIN(filesize - chunkOffset, readLength);
        fseek(file_ptr, chunkOffset, SEEK_SET);
        res = fread(buf + bufferOffset, readLength, 1, file_ptr); // read bytes to buffer
      }

      fclose(file_ptr);
      if (res == -1) {
        res = -errno;
        return res;
      }
    }
    
    bufferRemaining -= readLength;
    bufferOffset += readLength;
    if (bufferRemaining == 0)
      return size;
#endif
  }
//[cf]
  return 0;
}
//[cf]
//[of]:static int blockfs_writeblocks(const char *buf, size_t size, off_t offset)
static int blockfs_writeblocks(const char *buf, size_t size, off_t offset)
{
//[c]
//[c]if entire block is zero
//[c]  do not write to file
//[c]  if file exist, delete it.
//[c]allocate memory for a chunk file bitmap to use for detecting empty chunks
//[c]struct 
//[c]
//[of]:  setup vars


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
  char fileName[256];
  char filePath[256];

  FILE *file_ptr = NULL;

//[c]max buffer write size from libfuse is 128k
//[c]1 chunk at 5m is composed of 40 writes
//[c]
//[c]if chunk is all 0s, delete it.
//[c]  how to figure out chunk is empty without loading the whole thing into memory?
//[c]static struct chunkmeta {
//[c]  // each bit represents
//[c]  const char *bitmap;
//[c]  unsigned int chunkId;
//[c]
//[c]} chunkmeta;
//[c]
//[c]chunkmeta chunk
//[c]

//[cf]
//[of]:	while (bufferRemaining > 0) {
  while (bufferRemaining > 0) {
    chunkId = (offset + bufferOffset) / gl_chunkSize;
    chunkOffset = (offset + bufferOffset) % gl_chunkSize;
    driveIndex = chunkId % gl_mountSplits + 1;

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


    strcpy(fileName, options.local_store);

    strcat(fileName, driveIndexText);

    strcat(fileName, "/");
    strcat(fileName, dirLevel1);

    strcat(fileName, "/");
    strcat(fileName, dirLevel2);

    strcat(fileName, "/");
    strcat(fileName, dirLevel3);
    strcpy(filePath, fileName);

    strcat(fileName, "/");
    strcat(fileName, dirLevel4);
    strcat(fileName, ".dat");
//[cf]

//[c]    if filename has not changed
//[c]      no need to close and re-open the same file

    writeLength = MIN(gl_chunkSize - chunkOffset, bufferRemaining);

//[of]:    #if SHOW_MESSAGES_SIZES == 1
    #if SHOW_MESSAGES_SIZES == 1
    printft("offset %lu -- bufferRemaining %lu -- writeLength %lu -- bufferOffset %lu -- chunkId %lu -- chunkOffset %lu",
      offset,
      bufferRemaining,
      writeLength,
      bufferOffset,
      chunkId,
      chunkOffset
    );
    #endif
//[cf]

#if USE_NEW_DEV_CODE == 1
    if (writeLength == gl_chunkSize && is_empty(buf + bufferOffset, writeLength)) {
      //if entire chunk is zero, delete it.
//[of]:      #if SHOW_MESSAGES_READ_AND_SKIPS == 1
      #if SHOW_MESSAGES_READ_AND_SKIPS == 1
        sprintf(gl_debugMessageCurrent, "skipping/removing - %s", fileName);
        if (strcmp(gl_debugMessageCurrent, gl_debugMessageLast) != 0) {
          printft("%s", gl_debugMessageCurrent);
          strcpy(gl_debugMessageLast,gl_debugMessageCurrent);
        }
      #endif
//[cf]
      if( access( fileName, F_OK ) == 0 ) {
        // file exists
        if (remove(fileName) != 0) printf("unable to delete -- %s\n", fileName);
//[of]:        #if SHOW_MESSAGES_WRITES == 1
        #if SHOW_MESSAGES_WRITES == 1
        else {
          sprintf(gl_debugMessageCurrent, "deleted ----------- %s", fileName);
          if (strcmp(gl_debugMessageCurrent, gl_debugMessageLast) != 0) {
            printft("%s", gl_debugMessageCurrent);
            strcpy(gl_debugMessageLast,gl_debugMessageCurrent);
          }
        }
        #endif
//[cf]
      }
    }
    else if (is_empty(buf + bufferOffset, writeLength) && access( fileName, F_OK ) == -1) {
#else
    if (is_empty(buf + bufferOffset, writeLength) && access( fileName, F_OK ) == -1) {
#endif
      //if buffer section is empty and file does not exist, do nothing.
//[of]:      #if SHOW_MESSAGES_READ_AND_SKIPS == 1
      #if SHOW_MESSAGES_READ_AND_SKIPS == 1
        sprintf(gl_debugMessageCurrent, "skipping write ---- %s", fileName);
        if (strcmp(gl_debugMessageCurrent, gl_debugMessageLast) != 0) {
          printft("%s", gl_debugMessageCurrent);
          strcpy(gl_debugMessageLast,gl_debugMessageCurrent);
        }
      #endif
//[cf]
      ;
    }
    else {
//[of]:      #if SHOW_MESSAGES_WRITES == 1
      #if SHOW_MESSAGES_WRITES == 1
        sprintf(gl_debugMessageCurrent, "writing/creating -- %s", fileName);
        if (strcmp(gl_debugMessageCurrent, gl_debugMessageLast) != 0) {
          printft("%s", gl_debugMessageCurrent);
          strcpy(gl_debugMessageLast,gl_debugMessageCurrent);
        }
      #endif
//[cf]
      
      //mkdir path if it doesn't exist
      _mkdir(filePath);

      //create flie if it doesn't exist
      if( access( fileName, F_OK ) == -1 ) {
        file_ptr = fopen(fileName,"w");
        fclose(file_ptr);
      }
  
      file_ptr = fopen(fileName,"r+b");  // r+ open for update and writing, b for binary
      fseek(file_ptr, chunkOffset, SEEK_SET);
      fwrite(buf + bufferOffset, writeLength, 1, file_ptr); // write bytes from buffer
      fclose(file_ptr);
    }

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
    stbuf->st_size = (long long) gl_blockFileSize;
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

  if (offset + size > gl_blockFileSize)
    return -ENOSPC;

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

  if (offset + size > gl_blockFileSize)
    return -ENOSPC;

  size = blockfs_writeblocks(buf, size, offset);

  return size;
}
//[cf]
//[of]:static int blockfs_fsync(const char *path, int isdatasync,
static int blockfs_fsync(const char *path, int isdatasync,
         struct fuse_file_info *fi)
{
  /* Just a stub.   This method is optional and can safely be left
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
  .getattr  = blockfs_getattr,
  .readdir  = blockfs_readdir,
  .open     = blockfs_open,
  .read     = blockfs_read,
  .write    = blockfs_write,
  .fsync    = blockfs_fsync,
};
//[cf]
//[of]:int main(int argc, char *argv[])
int main(int argc, char *argv[])
{
  int ret;
  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

  // set last debug message to empty
  gl_debugMessageLast[0] = '\0';

  /* Set defaults -- we have to use strdup so that
     fuse_opt_parse can free the defaults if other
     values are specified */
//[c]  todo:
//[c]    must add trailing / if it does not exist to the local_store option.
//[c]    if last character in string is not / inc 1 and add /\0
  options.local_store = strdup("/mnt/localfs/");
  options.blockfile_size = strdup("2048");
  options.chunk_size = strdup("5");
  options.mount_splits = strdup("6");

  /* Parse options */
  if (fuse_opt_parse(&args, &options, option_spec, NULL) == -1)
    return 1;

  // enable single thread operation cuz this doesn't work with multi-threaded
  fuse_opt_add_arg(&args, "-s");

  #if SHOW_MESSAGES_MISC == 1
  fuse_opt_add_arg(&args, "-f");
  printft("and we're off!");
  #endif

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

  gl_blockFileSize = (long long) 1024 * 1024 * atol(options.blockfile_size);
  gl_chunkSize = (long int) 1024 * 1024 * atoi(options.chunk_size);
  gl_mountSplits = atoi(options.mount_splits);

  ret = fuse_main(args.argc, args.argv, &blockfs_oper, NULL);
  fuse_opt_free_args(&args);
  return ret;
}
//[cf]
//[cf]
