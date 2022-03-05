#!/usr/bin/env python
#[of]:imports
from __future__ import with_statement

from errno import EACCES, ENOENT, EROFS, EEXIST
from stat import S_IFDIR, S_IFLNK, S_IFREG
from threading import Lock
from time import time
from ctypes import create_string_buffer

import os, os.path, sys

from fuse import FUSE, FuseOSError, Operations, LoggingMixIn

import shutil
import zlib
import gzip
import hashlib
from Crypto.Cipher import AES
import gc
#[cf]
#[of]:static globals
CHUNKSIZE = (1024 * 1024 * 5)
#[cf]
#[of]:class BlockFS(LoggingMixIn, Operations)\:
class BlockFS(LoggingMixIn, Operations):
#[of]:	def __init__(self,target_store, cache_store, max_cache_size, blockfile_size, password=None)\:
	def __init__(self,target_store, cache_store, max_cache_size, blockfile_size, password=None):
		self.targetstore = os.path.realpath(target_store)
		with open('gdrive.list') as targetstore_listfile:
			self.targetstore_list = targetstore_listfile.read().splitlines()
		targetstore_listfile.close()

		self.cachestore = os.path.realpath(cache_store)
		self.maxcachesize = max_cache_size # not used yet
		self.rwlock = Lock()
		self.fd = 0
		self.memblocks = {}
		self.memory_blocks_total = 0
		self.nchunks = blockfile_size * 1024 * 1024/CHUNKSIZE
		
		self.key = password
		now = time()
		self.fileprops = dict(st_mode=(S_IFREG | 0755), st_nlink=1,
                                st_size=self.nchunks * CHUNKSIZE, 
                                st_blocks=self.nchunks * CHUNKSIZE / 512, #st_blksize=CHUNKSIZE,
                                st_ctime=now, st_mtime=now, st_atime=now)
		self.rootprops = dict(st_mode=(S_IFDIR | 0755), st_ctime=now, 
			st_mtime=now, st_atime=now, st_nlink=2)
#[cf]
#[of]:	called when fs is umounted
#[of]:	def destroy(self, path)\:
	def destroy(self, path):
		self.log.info("before lock destroy")
		with self.rwlock:
			self.log.info("after lock destroy")
			self.log.info("calling flush with force write")
			flush_status = self.flushmemoryblocks(force_write=True)
		return flush_status
#[cf]
#[cf]
#[c]
#[of]:	block methods
#[of]:	def readblocks(self, offset, size)\:
	def readblocks(self, offset, size):
#[c]		read block
#[c]			if exist at m; <-m
#[c]			if exist at c; <-m<-c
#[c]			if exist at t; <-m<-t
#[c]			else <-m<-0
		self.log.info('readblocks getting data from %d %d' % (offset, size))
		data = b''
		begin = offset % CHUNKSIZE
		blockbegin = begin
		sizeleft = size

		# merge into contiguous
		i = offset / CHUNKSIZE
		while sizeleft > 0:
			data += self.readmemoryblock(i)['data']
			if blockbegin + sizeleft <= CHUNKSIZE:
				break
			sizeleft -= CHUNKSIZE - blockbegin
			blockbegin = 0
			i += 1
		# get right part
		r =  data[begin:begin + size]
		self.log.info('readblocks %d %d done; returning %d bytes' % (offset, size, len(r)))
		return r
#[cf]
#[of]:	def writeblocks(self, offset, data)\:
	def writeblocks(self, offset, data):
		return self.writememoryblock(offset, data)
#[cf]
#[of]:	memory block methods
#[of]:	def readmemoryblock(self, i)\:
	def readmemoryblock(self, i):
		if self.memory_blocks_total > 40:
			raise FuseOSError(EACCES)
		if self.memory_blocks_total != len(self.memblocks):
			self.memory_blocks_total = len(self.memblocks)
			if self.memory_blocks_total > 40:
				self.log.info("calling flush with force write")
				self.flushmemoryblocks(force_write=True)
				self.memory_blocks_total = len(self.memblocks)
			self.log.info('cache change %d' % self.memory_blocks_total)
		if i not in self.memblocks:
#[c]			attempt to read block from backing stores, else create an empty block in memory	
			if not self.checkcacheblock(i):
				self.log.warning('creating as empty block %d' % i)
				#buf = bytearray(CHUNKSIZE)
				buf = b'\0' * CHUNKSIZE
				self.memblocks[i] = dict(data=buf, t_write=time(), t_read=time(), dirty=True)
			else:
				self.memblocks[i] = self.readcacheblock(i)

		self.memblocks[i]['t_read'] = time()
		
		self.log.info('block %d info: %s' % (i, 
			dict(t_read=self.memblocks[i]['t_read'], 
				t_write=self.memblocks[i]['t_write'], 
				dirty=self.memblocks[i]['dirty'])) )
		return self.memblocks[i]
#[cf]
#[of]:	def writememoryblock(self, force_write=False)\:
	def writememoryblock(self, offset, data):
#[c]		write blocks to memory
		size = len(data)
		self.log.info('creating block %d %d' % (offset, size))
		begin = offset % CHUNKSIZE
		databegin = 0
		sizeleft = size

		now = time()
		i = offset / CHUNKSIZE
		while sizeleft > 0:
			chunk = self.readmemoryblock(i)
			length = min(CHUNKSIZE - begin, sizeleft)
			# take from data[databegin:databegin + length]
			# write to begin:min(begin + size, CHUNKSIZE)
			
			if chunk['data'][begin:begin + length] != data[databegin:databegin + length]:
				self.log.info('modifying block %d: memblock %d : %d <- data %d : %d' % (i, begin, begin + length, databegin, databegin + length))
				chunk['t_write'] = now
				chunk['t_read'] = now
				chunk['dirty'] = True
				chunk['data'] = chunk['data'][0:begin] + \
					data[databegin:databegin + length] + \
					chunk['data'][begin+length:]
				# chunk['data'][begin:begin + length] = data[databegin:databegin + length]
			
			databegin += length
			sizeleft -= length
			if sizeleft == 0:
				break
			begin = 0
			i += 1
#[c]			if i > self.nchunks:
#[c]				raise error
			
		self.log.info('creating block %d %d done' % (offset, size))
		return size
#[cf]
#[of]:	def flushmemoryblocks(self, force_write=False)\:
	def flushmemoryblocks(self, force_write=False):
		self.log.info('flushing: starting')
		to_remove = []
		for i, props in self.memblocks.iteritems():
#[of]:			write memblocks to disk if needed
			if (force_write or time() - props['t_write'] > 10) and props['dirty']:
				self.log.info('flushing: writing dirty block %d time %d' % (i, time()))
#[of]:				writecacheblock
				r = self.writecacheblock(i)
				if r != 0:
					self.log.error('flushing: %d writing failed' % i)
					return r
#[cf]
#[cf]
#[of]:			queue unused memblocks to be removed from memory
			if (force_write or (time() - props['t_read'] > 60 and len(self.memblocks) > 20)) and not props['dirty']:
				# in seconds?
				# number of memblocks 20 * 5M = 100M
				if i not in to_remove:
					to_remove.append(i)
#[cf]
		for i in to_remove:
			self.log.info("flushing: memblock %d time %d" % (i,time()))
			del self.memblocks[i]
			self.log.warning('flushing: memblock %d removed' % i)
		self.log.info('flushing: finished')
		return 0

#[c]		self.log.info('flushing: %d is %.1fs/%.1fs old' % (i, time() - props['t_write'], time() - props['t_write']))
#[c]		self.log.info('flushing: block %d is stale' % i)
#[cf]
#[cf]
#[of]:	cache block methods
#[of]:	def checkcacheblock(self, i)\:
	def checkcacheblock(self, i):
		path = str.format('%0.8d' % i)
		path = os.path.join(self.cachestore, path[0:2],path[2:4],path[4:6],path[6:8]+'.dat')
		if os.path.exists(path):
			return True
		return self.checktargetblock(i)
#[cf]
#[of]:	def readcacheblock(self, i)\:
	def readcacheblock(self, i):
		blocknum = str.format('%0.8d' % i)
		basepath = os.path.join(blocknum[0:2],blocknum[2:4],blocknum[4:6])
		cachefile = os.path.join(self.cachestore,basepath,blocknum[6:8]+'.dat')
		for j in [1,2,3]:
#[of]:			if not os.path.exists(cachefile)\: # try to pull from target store
			if not os.path.exists(cachefile):
#[c]				copy block file from target to cache
				self.log.warning('copying block %d from target to cache' % i)
				targetfile = os.path.join(self.targetstore,basepath,blocknum[6:8]+'.dat')
				cachepath = os.path.join(self.cachestore,basepath)
				self.make_sure_path_exists(cachepath)
	
#[c]				self.log.warning('copying %s to %s' % (os.path.join('/root/gdrive/.storage',basepath,blocknum[6:8]+'.dat'), cachefile))
				os.chdir('/root/gdrive')   # Change current working directory
				os.system('drive pull -quiet -no-prompt --exclude-ops "delete" -ignore-checksum=false '+str(os.path.join('/root/gdrive/.storage',basepath,blocknum[6:8]+'.dat')))   # Run the command in the system shell
#[c]				shutil.copy2(targetfile, cachefile)
#[cf]
			self.log.warning('loading block %d from cache' % i)
			f = file(cachefile, 'rb')
			try:
				buf = self.decompress(f.read(), i)
			except:
				f.close()
				os.remove(cachefile)
				continue
			break
		f.close()
		return dict(data=buf, t_write=time(), t_read=time(), dirty=False)
#[cf]
#[of]:	def writecacheblock(self, i)\:
	def writecacheblock(self, i):
		self.log.warning('writing block %d into cache fs' % i)
		cblock = str.format('%0.8d' % i)
		path = os.path.join(self.cachestore, cblock[0:2],cblock[2:4],cblock[4:6])
		self.make_sure_path_exists(path)
		path = os.path.join(path, '%s.dat' % cblock[6:8])
		f = file(path, 'wb')
		f.write(self.compress(self.memblocks[i]['data'], i))
		f.flush()
		os.fsync(f.fileno())
		f.close()
		self.memblocks[i]['dirty'] = False
		self.log.warning('writing block %d into cache fs done' % i)
		return 0
#[cf]
#[cf]
#[of]:	target block methods
#[of]:	def checktargetblock(self, i)\:
	def checktargetblock(self, i):
		path = str.format('%0.8d' % i)
		path = os.path.join(path[0:2],path[2:4],path[4:6],path[6:8]+'.dat')
		if path in self.targetstore_list:
#[c]		path = os.path.join(self.targetstore, path[0:2],path[2:4],path[4:6],path[6:8]+'.dat')
#[c]		if os.path.exists(path):
			return True
		return False
#[cf]
#[cf]
#[cf]
#[of]:	mounted filesystem methods
#[of]:	def access(self, path, mode)\:
	def access(self, path, mode):
		if not os.access(self.targetstore, mode):
			raise FuseOSError(EACCES)
#[cf]
#[of]:	def getattr(self, path, fh=None)\:
	def getattr(self, path, fh=None):
		if path == '/':
			return self.rootprops
		elif path == '/block':
			return self.fileprops
		else:
			raise FuseOSError(ENOENT)
#[cf]
#[of]:	def readdir(self, path, fh)\:
	def readdir(self, path, fh):
		return ['.', '..', 'block']
#[cf]
#[of]:	def statfs(self, path)\:
	def statfs(self, path):
		self.log.warning('statfs')
		return dict(f_bsize=CHUNKSIZE, f_blocks=self.nchunks, f_bavail=0)
#[cf]
#[of]:	file methods
#[of]:	def open(self, path, flags)\:
	def open(self, path, flags):
		self.fd += 1
		return self.fd
#[cf]
#[of]:	def read(self, path, size, offset, fh)\:
	def read(self, path, size, offset, fh):
		self.log.info('read of %s @%d %d' % (path, offset, size))
		with self.rwlock:
			return self.readblocks(offset, size)
		self.log.error('read of %s @%d %d failed!' % (path, offset, size))
		raise FuseOSError(EROFS)
#[cf]
#[of]:	def write(self, path, data, offset, fh)\:
	def write(self, path, data, offset, fh):
		with self.rwlock:
			return self.writeblocks(offset, data)
		raise FuseOSError(EROFS)
#[cf]
#[of]:	def flush(self, path, fh)\:
	def flush(self, path, fh):
		self.log.info("before lock flush")
		with self.rwlock:
			self.log.info("after lock flush")
			self.log.info("calling flush")
			flush_status = self.flushmemoryblocks()
		return flush_status
#[cf]
#[of]:	def fsync(self, path, datasync, fh)\:
	def fsync(self, path, datasync, fh):
		self.log.info("before lock fsync")
		with self.rwlock:
			self.log.info("after lock fsync")
			self.log.info("calling flush")
			flush_status = self.flushmemoryblocks()
		return flush_status
#[cf]
#[cf]
#[cf]
#[of]:	local methods
#[of]:	def compress(self, data, i)\:
	def compress(self, data, i):
		dataz =  zlib.compress(data)
		if self.key:
			# padding idea from https://gist.github.com/twkang/4417903
			pad = 16 - len(dataz) % 16
			dataz = dataz + pad * chr(pad)
			iv = hashlib.sha256(str(i)).digest()[:16]
			encryptor = AES.new(self.key, AES.MODE_CBC, iv)
			dataz = encryptor.encrypt(dataz)
		return dataz
#[cf]
#[of]:	def decompress(self, dataz, i)\:
	def decompress(self, dataz, i):
		if self.key:
			iv = hashlib.sha256(str(i)).digest()[:16]
			decryptor = AES.new(self.key, AES.MODE_CBC, iv)
			dataz = decryptor.decrypt(dataz)
			pad = ord(dataz[-1])
			dataz = dataz[:-pad]
		return zlib.decompress(dataz)
#[cf]
#[of]:	def make_sure_path_exists(self, path)\:
	def make_sure_path_exists(self, path):
		if not os.path.exists(path):
			try:
				os.makedirs(path)
			except OSError as exception:
				if exception.errno != errno.EEXIST:
					raise FuseOSError(EEXIST)	
#[cf]
#[cf]
#[c]
#[c]	Provides a large data block. In the backing directory, 5MB blocks of
#[c]	compressed, encrypted data are stored.
#[c]	
#[c]	r=remote block store
#[c]	c=cache block store
#[c]	m=memory block
#[c]
#[c]	#read memory pool = 70%
#[c]	#write memory pool = 30%
#[c]	
#[c]	write block -> m
#[c]	flush block -> c
#[c]	flush by time
#[c]	if c > max_cache_size
#[c]		find all files older than max_file_age
#[c]		for each file found
#[c]			if size/date are different
#[c]				compress file in c
#[c]				encrypt file in c
#[c]				copy file from c to r
#[c]			rm file from c
#[c]
#[c]	
#[cf]
#[of]:if __name__ == '__main__'\:
if __name__ == '__main__':
#[c]blockfs {target store} {cache store} {cache size in MBs} {mountpoint} {block file size in MBs} [password-file]'
	if not (len(sys.argv) == 6 or len(sys.argv) == 7):
		print('usage: %s {target store} {cache store} {cache size in MBs} {mountpoint} {block file size in MBs} [password-file]' % sys.argv[0])
		sys.exit(1)
	
	import logging
#[c]	logging.basicConfig(filename='blockfs-debug.log',level=logging.DEBUG)
#[c]	logging.basicConfig(filename='blockfs-debug.log',level=logging.INFO)
	logging.basicConfig(filename='blockfs-debug.log',level=logging.WARNING)
#[c]	logging.basicConfig(filename='blockfs-debug.log',level=logging.ERROR)
	
	target_store = sys.argv[1]
	cache_store = sys.argv[2]
	max_cache_size = int(sys.argv[3])
	mount_point= sys.argv[4]
	blockfile_size = int(sys.argv[5])

	password = None
	if len(sys.argv) > 6:
		passwordfile = sys.argv[6]
		if len(file(passwordfile, 'rb').read()) != 32:
			password = hashlib.sha256(file(passwordfile, 'rb').read()).digest()[:32]
		else:
			password = file(passwordfile, 'rb').read()
		logging.info(' '.join(['using password', password]))
		gc.collect()
	b = BlockFS(target_store, cache_store, max_cache_size, blockfile_size, password)
	fuse = FUSE(b, mount_point, nothreads=True, foreground=True,
		allow_root=True)
#[cf]
