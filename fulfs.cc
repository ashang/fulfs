
#define FUSE_USE_VERSION 25


#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>

#include <stdlib.h>
#include <limits.h>

//#include <tr1/memory>

#include <string>
#include <iostream>
#include <map>
#include <vector>
//#include <ext/hash_map>

using namespace std;

/*
#include "lockable.h"
#include "lockguard.h"

*/
//using namespace __gnu_cxx;
//using namespace std::tr1;

string fromdir, todir;
uint64_t ncsize=2147483647;


inline void incName(string &name) {
   name[name.size()-1]++;
   if(name[name.size()-1] > 'z') {
      name[name.size()-2]++;
      name[name.size()-1]='a';
   }
}

inline void appSuf(string &toadd, unsigned int i) {
   toadd+=".";
   toadd+=('a'+i/26);
   toadd+=('a'+i%26);
}

// global lock for all decission critical changes
pthread_mutex_t nameres_lock = PTHREAD_MUTEX_INITIALIZER;

class lf /*: public lockable*/ {
   public:
      lf(const char *path) { 
         csize=ncsize; // default for writters, can be changed by existing value from the first chunk
         ccount=0;
         cached_fd=-1;
         cached_local_off=-1;
         cached_chunk=-1;

         struct stat stbuf;
         if(!path)
            return;
         virtname=fromdir+path;

         if(0==lstat(virtname.c_str(), &stdata)) { // unchunked file, nothing to do here
            ccount=1;
            return;
         }

         // get basic stat data from the first chunk, if exists, and stop otherwise
         if(lstat((virtname+".aa").c_str(), &stdata))
            return;

         // from here on assume that .aa file exists and .ab must exist too, and
         // .aa defines the chunk size and all usefull attributes


         if(stdata.st_size)
            csize=stdata.st_size; // chunk size to be saved as default for this object
         stdata.st_size=0; // not double-counting the first chunk
         for(unsigned int i=0;i<676;i++) {
            if(0==lstat(makeChunkName(i).c_str(), &stbuf)) {
               if( uint64_t(stbuf.st_size) > csize) {
                  ccount=-1;
                  return;
                  // FIXME, report error and mark invalid
                  //compflags.clear();
               }
               else {
                  ccount=i+1;
                  stdata.st_size+=stbuf.st_size;
                  if( (uint64_t)stbuf.st_size < csize)
                     // last chunk
                     return;
               }
            }
            else {
               ccount=i; // one less failed i -> ok
               return;
            }
         }
      };

      inline string getCname(unsigned int i) {
         if(int(i)>=ccount)
            return "";
         if(ccount<2) // same name
            return virtname;
         return makeChunkName(i);
      }

      ~lf() { 
         if(cached_fd>=0)
            close(cached_fd);
      }

      void getFstat(struct stat *stbuf) {
         memcpy(stbuf, &stdata, sizeof(stdata));
      }

      bool exists() { 
         return (ccount>0);
      }

      bool prep_open(int flags) {
         if(0==ccount) {
            if((flags&O_CREAT))
               ccount=1; // force the use of the virtname for first chunk
            else {
               errno=ENOENT;
               return false;
            }
         }
         this->flags=flags;
         return true;
      }

      int getReaderAt(off_t offset) {

         unsigned int local_chunk = offset/csize;
         int64_t local_off=offset%csize;

         // try it either the easy or the hard way
         if( ! (cached_fd>=0 && local_chunk==(unsigned int)cached_chunk && local_off == (unsigned int) cached_local_off))
         { // don't rely on anything, make it safe
            if(cached_fd>=0)
               close(cached_fd);
            
            pthread_mutex_lock(&nameres_lock); // getCname resolution must be valid when open happens
            cached_fd = open(getCname(local_chunk).c_str(), flags); // abuse cached_fd here, does not matter
            pthread_mutex_unlock(&nameres_lock);
            if(cached_fd<0)
               return cached_fd;
            cached_chunk=local_chunk;

            if(local_off!=lseek(cached_fd, local_off, SEEK_SET))
               return -1;
            cached_local_off=local_off;

         }

         return cached_fd;
      }
      void moveOffset(off_t offset) {
         cached_local_off=offset%csize;
      }

      inline void pad_before(unsigned int current_chunk) {
         // make sure the previous have the right size
         unsigned int i = 0;
         if(ccount>=2)
            i = ccount-2;
         for(;i<current_chunk;i++)
            truncate(getCname(i).c_str(), csize);
      }

      inline int unlink_after(unsigned int current_chunk) {

         int ret(0);

         // drop the trailing
         for(int i(current_chunk+1); i<ccount; i++)
         {
            if(unlink(getCname(i).c_str()) && 0==ret) // remember only the first error
               ret=errno;
         }

         return ret;
      }

      int trunc(off_t size) {
         unsigned int current_chunk = size/csize;
         int64_t last_off=size%csize;

         // this may suck with slowly deleting filesystems
         pthread_mutex_lock(&nameres_lock);

         pad_before(current_chunk);
         unlink_after(current_chunk);
         ccount=current_chunk+1;

         //cerr << "truncating " << getCname(i).c_str() << " to " <<coff <<endl;
         truncate(getCname(current_chunk).c_str(), last_off);

         stdata.st_size=size;
         // cerr << "neu: " << ccnt << " chunks, " << size << " bytes\n";
         //
         pthread_mutex_unlock(&nameres_lock);
         return 0;
      }

      int getWriterAt(off_t offset, size_t &rest) {
         unsigned int current_chunk = offset/csize;
         if(current_chunk>=26*26) {
            errno=ENOSPC;
            return -1;
         }
         int64_t local_off=offset%csize;
         if( ! (cached_fd>=0 && current_chunk==(unsigned int)cached_chunk && local_off == (unsigned int) cached_local_off))
         {
            if(cached_fd>=0)
               close(cached_fd);

            pthread_mutex_lock(&nameres_lock);
            if(local_off==0 && current_chunk == 1 && ccount==1)
            {
                // time to rename, must be locked here
                rename(virtname.c_str(), (virtname+".aa").c_str()); // FIXME: error handling
            }

            pad_before(current_chunk);

            if(int(current_chunk) > ccount-1)
               ccount=current_chunk+1;

            //cerr << "> " << getCname(chunk).c_str() << " flags: " << (flags|O_CREAT) <<endl;
            cached_fd = open(getCname(current_chunk).c_str(), O_CREAT|O_RDWR, S_IRUSR|S_IWUSR|S_IRGRP);
            pthread_mutex_unlock(&nameres_lock);
            if(cached_fd<0)
               return -1;
            cached_chunk=current_chunk;
            if(local_off!=lseek(cached_fd, local_off, SEEK_SET))
               return -1;
            cached_local_off=local_off;
         }
         if(rest > csize-cached_local_off)
            rest=csize-cached_local_off;
         return cached_fd;
      }

   private:

         // signed types, negative values for invalid
      int cached_fd;
      int cached_chunk;
      int64_t cached_local_off;

      int ccount;
      uint64_t csize;

      string virtname;
      struct stat stdata;
      int flags;

      inline string makeChunkName(unsigned int i) {
         string ret=virtname;
         appSuf(ret, i);
         return ret;
      }
};

//typedef map < int, lf *> hotmap_t;
//typedef hotmap_t::iterator hot_iter;
//typedef std::tr1::scoped_ptr<lf> plf_t;
//#define plf_t scoped_ptr<lf>
//#define plf_t auto_ptr<lf>
// FIXME: keine eigene Map sondern pointer bei fuse mitsichern
//hotmap_t hotmap;

static int fulfs_getattr(const char *path, struct stat *stbuf)
{
   lf p(path);
   if(p.exists())
      p.getFstat(stbuf);
   else
      return -ENOENT; // FIXME: errno tracking in constructor
   return 0;
   /*
   int res;
   lf p(path);

   res = lstat(p.getCname(0).c_str(), stbuf);
   if (res == -1)
      return -errno;

   return 0;
   */
}


static int fulfs_fgetattr(const char *, struct stat *stbuf,
      struct fuse_file_info *fi)
{

   ((lf*)fi)->getFstat(stbuf);
   return 0;
}


static int fulfs_access(const char *path, int mask)
{
   int res;
   lf p(path);
   res = access(p.getCname(0).c_str(), mask);
   if (res == -1)
      return -errno;

   return 0;
}

static int fulfs_readlink(const char *path, char *buf, size_t size)
{
   int res;

   res = readlink(path, buf, size - 1);
   if (res == -1)
      return -errno;

   buf[res] = '\0';
   return 0;
}


static int fulfs_opendir(const char *path, struct fuse_file_info *fi)
{
/*
 * DIR *dp = opendir(path);
   if (dp == NULL)
      return -errno;

   fi->fh = (unsigned long) dp;
   */
   return 0;
}

static inline DIR *get_dirp(struct fuse_file_info *fi)
{
   return (DIR *) (uintptr_t) fi->fh;
}

static int fulfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
      off_t offset, struct fuse_file_info *fi)
{
   
   map<string, struct dirent> smap;
   DIR *dp = opendir((fromdir+path).c_str());
   if(!dp)
      return 0;
   struct dirent *de;
   while(true) {
      de=readdir(dp);
      if(!de)
         break;
      smap[de->d_name]=*de;
   }
   closedir(dp);
   map<string, struct dirent>::iterator tmpit, it=smap.begin();
   string toskip;

proc_next_no_inc:
   for(;it!=smap.end();it++) {
      if(!toskip.empty()) {
         if(toskip!=it->first)
            toskip.clear();
         else {
            tmpit=it;
            it++;
            smap.erase(tmpit);
            // check next name
            incName(toskip);
            goto proc_next_no_inc;
         }
      }
      string key=it->first; // the reported name
      string::size_type s=it->first.size();
      if(s>3 && it->first.substr(s-3, 3)==".aa") { // hit, look for company
         toskip=it->first;
         key.erase(s-3, s);
         incName(toskip);
      }
      struct stat st;
      memset(&st, 0, sizeof(st));
      st.st_ino = it->second.d_ino;
      st.st_mode = it->second.d_type << 12;
      if (filler(buf, key.c_str(), &st, 0))
         break;
   }

   return 0;
}

static int fulfs_releasedir(const char *path, struct fuse_file_info *fi)
{

   /*
   DIR *dp = get_dirp(fi);
   (void) path;
   closedir(dp);
   */
   return 0;
}

static int fulfs_mknod(const char *path, mode_t mode, dev_t rdev)
{
   int res;

   string p=fromdir+path;
   path=p.c_str();

   if (S_ISFIFO(mode))
      res = mkfifo(path, mode);
   else
      res = mknod(path, mode, rdev);
   if (res == -1)
      return -errno;

   return 0;
}

static int fulfs_mkdir(const char *path, mode_t mode)
{
   int res;

   res = mkdir((fromdir+path).c_str(), mode);
   if (res == -1)
      return -errno;

   return 0;
}

static int fulfs_unlink(const char *path)
{
   lf p(path);
   return -p.unlink_after(-1);
}

static int fulfs_rmdir(const char *path)
{
   int res;
   res = rmdir((fromdir+path).c_str());
   if (res == -1)
      return -errno;

   return 0;
}

static int fulfs_symlink(const char *from, const char *to)
{
   int res;

   res = symlink(from, to);
   if (res == -1)
      return -errno;

   return 0;
}

static int fulfs_rename(const char *from, const char *to)
{
   lf p(from);
   for(int i=0,err=0;;i++) {
      string n=p.getCname(i);
      if(n.empty())
         return (err?-errno:0);
      string neu(fromdir+to);
      appSuf(neu, i);
      //cerr << "rename, " << n << " zu "  << neu <<endl;
      err+=rename(n.c_str(), neu.c_str());
   }
   return 0;
}

static int fulfs_link(const char *from, const char *to)
{
   lf p(from);
   for(int i=0,err=0;;i++) {
      string n=p.getCname(i);
      if(n.empty())
         return (err?-errno:0);
      string neu(fromdir+to);
      appSuf(neu, i);
      // summarize all -1
      err+=link(n.c_str(), neu.c_str());
   }
   return 0;

}

static int fulfs_chmod(const char *path, mode_t mode)
{
   return -EPERM; // should be atomic, unless this is implemented, don't allow at all
   lf p(path);
   for(int i=0,err=0;;i++) {
      string n=p.getCname(i);
      if(n.empty())
         return (err?-errno:0);
      err+=chmod(n.c_str(), mode);
   }
   return 0;
}

static int fulfs_chown(const char *path, uid_t uid, gid_t gid)
{
   return -EPERM; // should be atomic, unless this is implemented, don't allow at all
   lf p(path);
   for(int i=0,err=0;;i++) {
      string n=p.getCname(i);
      if(n.empty())
         return (err?-errno:0);
      err+=chown(n.c_str(), uid, gid);
   }
   return 0;
}

static int fulfs_truncate(const char *path, off_t size)
{
   lf p(path);

   if(p.exists()) {
      if(p.trunc(size)<0)
         return -errno;
   }
   else
      return -ENOENT;

   return 0;
}

static int fulfs_ftruncate(const char *path, off_t size,
      struct fuse_file_info *fi)
{

   lf *p= (lf *)fi->fh;

   if(p->trunc(size) < 0)
      return -errno;

   return 0;
}

static int fulfs_utime(const char *path, struct utimbuf *buf)
{
   lf p(path);
   if( utime(p.getCname(0).c_str(), buf) < 0)
      return -errno;
   // rest does not matter
   for(int i=1;;i++) {
      string n=p.getCname(i);
      //cerr << n<<endl;
      if(n.empty())
         break;
      utime(n.c_str(), buf);
   }
   return 0;
}

static int fulfs_open(const char *path, struct fuse_file_info *fi)
{
   lf *p = new lf(path);

   if( ! p->prep_open(fi->flags)) {
      delete p;
      return -errno;
   }
   fi->fh = (uintptr_t) p;
   return 0;
}

#if 0 // doesn't matter, use mknod
static int fulfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
   lf *p = new lf(path);

   if( ! p->prep_open(fi->flags|O_CREAT)) {
      delete p;
      return -errno;
   }
   fi->fh = (uintptr_t) p;
   return 0;

   /*
   fi->flags|=O_CREAT;
   return fulfs_open(path, mode, fi);




   cerr << "fulfs, creat: " << path<<endl;
   int fd;

   fd = open(path, fi->flags, mode);
   if (fd == -1)
      return -errno;

   fi->fh = fd;
   return 0;
   */
}
#endif

static int fulfs_read(const char *path, char *buf, size_t size, off_t offset,
      struct fuse_file_info *fi)
{
   lf *p = (lf*) fi->fh;
   uint64_t sofar(0);
   while(true) {
      int fr = p->getReaderAt(offset+sofar);
      if(fr<0)
         return sofar;
      ssize_t n=read(fr, buf, size);
      if(n<0)
         return -errno;
      p->moveOffset(n);
      size-=n;
      buf+=n;
      sofar+=n;
      //cerr << "got n:" << n << ", pos: " << sofar << ", size: " << size<<endl;
      if(n==0)
         return sofar; // EOF? reader will know
   }
   return 0;
}

static int fulfs_write(const char *path, const char *buf, size_t size,
      off_t offset, struct fuse_file_info *fi)
{
   lf *p = (lf*) fi->fh;
   uint64_t sofar(0);
   while(true) {
      size_t maxize=size;
      int writer = p->getWriterAt(offset+sofar, maxize);
      if(writer<0)
         return sofar;
      ssize_t n=write(writer, buf, maxize);
      //cerr << "written: " << n<<endl;
      if(n<0)
         return -errno;
      p->moveOffset(n);
      size-=n;
      buf+=n;
      sofar+=n;
      //cerr << "done n:" << n << ", pos: " << sofar << ", size: " << size<<endl;
      if(n==0 || size==0)
         return sofar; 
   }
   return 0;
}

static int fulfs_statfs(const char *path, struct statvfs *stbuf)
{
   int res;

   res = statvfs((fromdir+path).c_str(), stbuf);
   if (res == -1)
      return -errno;

   return 0;
}

static int fulfs_release(const char *path, struct fuse_file_info *fi)
{
   delete ( (lf*) fi->fh);

   return 0;
}

static int fulfs_fsync(const char *path, int isdatasync,
      struct fuse_file_info *fi)
{
   int res;
   (void) path;

#ifndef HAVE_FDATASYNC
   (void) isdatasync;
#else
   if (isdatasync)
      res = fdatasync(fi->fh);
   else
#endif
      res = fsync(fi->fh);
   if (res == -1)
      return -errno;

   return 0;
}


struct fuse_operations fulfs_oper;

void _ExitUsage() {
   cerr << "USAGE: fulfs rootDir mountPoint [chunkSize] [FUSE Mount Options]\n"
      << "The chunkSize argument specifies size for fresh files\n"
      << "(default 2GiB-1), suffixes for units are allowed, eg. 700M\n"
      << "valid FUSE Mount Options follow:\n";
    const char *argv[] = {"...", "-h"};
    fuse_main( 2, const_cast<char**>(argv), &fulfs_oper);
    exit(EXIT_FAILURE);
}

#define barf(x) { cerr <<endl << "ERROR: " << x <<endl<<endl; _ExitUsage(); }

int main(int argc, char *argv[])
{

   memset(&fulfs_oper, 0, sizeof(fulfs_oper));

   fulfs_oper.getattr	= fulfs_getattr;
   fulfs_oper.fgetattr	= fulfs_fgetattr;
   fulfs_oper.access	= fulfs_access;
   fulfs_oper.readlink	= fulfs_readlink;
   fulfs_oper.opendir	= fulfs_opendir;
   fulfs_oper.readdir	= fulfs_readdir;
   fulfs_oper.releasedir	= fulfs_releasedir;
   fulfs_oper.mknod	= fulfs_mknod;
   fulfs_oper.mkdir	= fulfs_mkdir;
   fulfs_oper.symlink	= fulfs_symlink;
   fulfs_oper.unlink	= fulfs_unlink;
   fulfs_oper.rmdir	= fulfs_rmdir;
   fulfs_oper.rename	= fulfs_rename;
   fulfs_oper.link	= fulfs_link;
   fulfs_oper.chmod	= fulfs_chmod;
   fulfs_oper.chown	= fulfs_chown;
   fulfs_oper.truncate	= fulfs_truncate;
   fulfs_oper.ftruncate	= fulfs_ftruncate;
   fulfs_oper.utime	= fulfs_utime;
//   fulfs_oper.create	= fulfs_create;
   fulfs_oper.open	= fulfs_open;
   fulfs_oper.read	= fulfs_read;
   fulfs_oper.write	= fulfs_write;
   fulfs_oper.statfs	= fulfs_statfs;
   fulfs_oper.release	= fulfs_release;
   fulfs_oper.fsync	= fulfs_fsync;

   umask(0);

   if(argc<3)
      barf("Needs a source and a target directory, see --help.");

   struct stat stbuf;
   if(stat(argv[1], &stbuf) || !S_ISDIR(stbuf.st_mode))
      barf("\n" << argv[1] << " is not a directory.\n\n");

   char *buf=new char[PATH_MAX+1];
   if(realpath(argv[1], buf))
      fromdir=buf;
   else {
      cerr << "Invalid source directory, " << argv[1] <<endl;
      exit(EXIT_FAILURE);
   }
   delete [] buf;

   if(stat(argv[2], &stbuf) || !S_ISDIR(stbuf.st_mode))
      barf(endl<< argv[2] << " is not a directory.");

   todir=argv[2];

   int fuseArgPos=3;

   if(argc>3) {
      // does that look like a number?
      char *p;
      uint64_t testsize=strtoll(argv[3], &p, 10);
      if(testsize
#  ifdef __USE_ISOC99_fscked
            && testsize != LLONG_MIN  && testsize!=LLONG_MAX 
#endif
        )
      {
         // is a number, great
         fuseArgPos=4;

         switch(*p) {
            case('\0'):
               break;
            case('k'):
               testsize*=1000;
               break;
            case('K'):
               testsize*=1000;
               break;
            case('m'):
               testsize*=1000000;
               break;
            case('M'):
               testsize*=1048576;
               break;
            case('g'):
               testsize*=1000000000;
               break;
            case('G'):
               testsize*=1073741824;
               break;
            default:
               barf(p << " is not a valid unit.");
         }
         ncsize=testsize;
      }
   }

   /* hide the from argument */
   argv[fuseArgPos-1]=argv[2]; // mountpoint
   argv[fuseArgPos-2]=argv[0]; // application path
   argv=&argv[fuseArgPos-2];
   argc=argc-fuseArgPos+2;

   return fuse_main(argc, argv, &fulfs_oper);
}

