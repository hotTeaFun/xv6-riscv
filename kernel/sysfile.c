//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
extern int sockalloc(struct file **f, uint32 raddr, uint16 lport, uint16 rport);
static int argfd(int n, int *pfd, struct file **pf) {
  int fd;
  struct file *f;

  argint(n, &fd);
  if (fd < 0 || fd >= NOFILE || (f = myproc()->ofile[fd]) == 0) return -1;
  if (pfd) *pfd = fd;
  if (pf) *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int fdalloc(struct file *f) {
  int fd;
  struct proc *p = myproc();

  for (fd = 0; fd < NOFILE; fd++) {
    if (p->ofile[fd] == 0) {
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}
struct inode *getip(char *path, uint depth, int omode) {
  // reached the max symbolic recursive depth
  if (depth > SYMLINK_MAX_DEPTH) {
    return 0;
  }
  struct inode *ip;
  // file not exist
  if ((ip = namei(path)) == 0) {
    return 0;
  }
  ilock(ip);
  if (!(omode & O_NOFOLLOW) && ip->type == T_SYMLINK) {
    // path reference to a symbolic link and O_NOFOLLOW flag is not set
    // recursively follow the symbolic link until a non-link file is reached
    char next[MAXPATH];
    // uint *poff;
    // printf("read\n");
    if (readi(ip, 0, (uint64)next, ip->size - MAXPATH, MAXPATH) == 0) {
      iunlock(ip);
      return 0;
    }
    iunlock(ip);
    return getip(next, depth + 1, omode);
  }
  iunlock(ip);
  return ip;
}
uint64 sys_dup(void) {
  struct file *f;
  int fd;

  if (argfd(0, 0, &f) < 0) return -1;
  if ((fd = fdalloc(f)) < 0) return -1;
  filedup(f);
  return fd;
}

uint64 sys_read(void) {
  struct file *f;
  int n;
  uint64 p;

  argaddr(1, &p);
  argint(2, &n);
  if (argfd(0, 0, &f) < 0) return -1;
  return fileread(f, p, n);
}

uint64 sys_write(void) {
  struct file *f;
  int n;
  uint64 p;

  argaddr(1, &p);
  argint(2, &n);
  if (argfd(0, 0, &f) < 0) return -1;

  return filewrite(f, p, n);
}

uint64 sys_close(void) {
  int fd;
  struct file *f;

  if (argfd(0, &fd, &f) < 0) return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64 sys_fstat(void) {
  struct file *f;
  uint64 st;  // user pointer to struct stat

  argaddr(1, &st);
  if (argfd(0, 0, &f) < 0) return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64 sys_link(void) {
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if (argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0) return -1;

  begin_op();
  if ((ip = namei(old)) == 0) {
    end_op();
    return -1;
  }

  ilock(ip);
  if (ip->type == T_DIR) {
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if ((dp = nameiparent(new, name)) == 0) goto bad;
  ilock(dp);
  if (dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0) {
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int isdirempty(struct inode *dp) {
  int off;
  struct dirent de;

  for (off = 2 * sizeof(de); off < dp->size; off += sizeof(de)) {
    if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if (de.inum != 0) return 0;
  }
  return 1;
}

uint64 sys_unlink(void) {
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if (argstr(0, path, MAXPATH) < 0) return -1;

  begin_op();
  if ((dp = nameiparent(path, name)) == 0) {
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if (namecmp(name, ".") == 0 || namecmp(name, "..") == 0) goto bad;

  if ((ip = dirlookup(dp, name, &off)) == 0) goto bad;
  ilock(ip);

  if (ip->nlink < 1) panic("unlink: nlink < 1");
  if (ip->type == T_DIR && !isdirempty(ip)) {
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if (ip->type == T_DIR) {
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode *create(char *path, short type, short major, short minor) {
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if ((dp = nameiparent(path, name)) == 0) return 0;

  ilock(dp);

  if ((ip = dirlookup(dp, name, 0)) != 0) {
    iunlockput(dp);
    ilock(ip);
    if (type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if ((ip = ialloc(dp->dev, type)) == 0) {
    iunlockput(dp);
    return 0;
  }

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if (type == T_DIR) {  // Create . and .. entries.
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if (dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      goto fail;
  }

  if (dirlink(dp, name, ip->inum) < 0) goto fail;

  if (type == T_DIR) {
    // now that success is guaranteed:
    dp->nlink++;  // for ".."
    iupdate(dp);
  }

  iunlockput(dp);

  return ip;

fail:
  // something went wrong. de-allocate ip.
  ip->nlink = 0;
  iupdate(ip);
  iunlockput(ip);
  iunlockput(dp);
  return 0;
}

uint64 sys_open(void) {
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  argint(1, &omode);
  if ((n = argstr(0, path, MAXPATH)) < 0) return -1;

  begin_op();

  if (omode & O_CREATE) {
    ip = create(path, T_FILE, 0, 0);
    if (ip == 0) {
      end_op();
      return -1;
    }
  } else {
    if ((ip = getip(path, 0, omode)) == 0) {
      end_op();
      return -1;
    }
    ilock(ip);
    if (ip->type == T_DIR && omode != O_RDONLY) {
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if (ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)) {
    iunlockput(ip);
    end_op();
    return -1;
  }

  if ((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0) {
    if (f) fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if (ip->type == T_DEVICE) {
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if ((omode & O_TRUNC) && ip->type == T_FILE) {
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64 sys_symlink(void) {
  char name[DIRSIZ], target[MAXPATH], path[MAXPATH];
  struct inode *dp, *ip;
  if (argstr(0, target, MAXPATH) < 0 || argstr(1, path, MAXPATH) < 0) {
    return -1;
  }
  begin_op();
  // target does not need to exist for the system call
  //  just store it in path inode's data block
  if ((ip = namei(target)) != 0 &&
      ip->type != T_DIR)  // it's ok if target doesn's exist
  {
    // if ip exist,increase nlink
    ilock(ip);
    ip->nlink++;
    iupdate(ip);
    iunlockput(ip);
  }
  if ((dp = namei(path)) == 0) {
    if ((dp = nameiparent(path, name)) == 0) {
      printf("NO inode corresponding to path's parent\n");
      goto bad;
    } else {
      if ((dp = create(path, T_SYMLINK, 0, 0)) ==
          0)  // last two arguments are for T_DEVICE only
      {
        printf("create symlink for (%s->%s) fail!\n", path, target);
        goto bad;
      } else {
        iunlock(dp);
      }
    }
  }
  ilock(dp);
  // store target in the end of directory dp's data block
  writei(dp, 0, (uint64)target, dp->size, MAXPATH);
  dp->type = T_SYMLINK;
  iunlockput(dp);
  end_op();
  return 0;

bad:
  end_op();
  return -1;
}

uint64 sys_mkdir(void) {
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if (argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0) {
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64 sys_mknod(void) {
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  argint(1, &major);
  argint(2, &minor);
  if ((argstr(0, path, MAXPATH)) < 0 ||
      (ip = create(path, T_DEVICE, major, minor)) == 0) {
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64 sys_chdir(void) {
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();

  begin_op();
  if (argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0) {
    end_op();
    return -1;
  }
  ilock(ip);
  if (ip->type != T_DIR) {
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64 sys_exec(void) {
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  argaddr(1, &uargv);
  if (argstr(0, path, MAXPATH) < 0) {
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for (i = 0;; i++) {
    if (i >= NELEM(argv)) {
      goto bad;
    }
    if (fetchaddr(uargv + sizeof(uint64) * i, (uint64 *)&uarg) < 0) {
      goto bad;
    }
    if (uarg == 0) {
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if (argv[i] == 0) goto bad;
    if (fetchstr(uarg, argv[i], PGSIZE) < 0) goto bad;
  }

  int ret = exec(path, argv);

  for (i = 0; i < NELEM(argv) && argv[i] != 0; i++) kfree(argv[i]);

  return ret;

bad:
  for (i = 0; i < NELEM(argv) && argv[i] != 0; i++) kfree(argv[i]);
  return -1;
}

uint64 sys_pipe(void) {
  uint64 fdarray;  // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  argaddr(0, &fdarray);
  if (pipealloc(&rf, &wf) < 0) return -1;
  fd0 = -1;
  if ((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0) {
    if (fd0 >= 0) p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if (copyout(p->pagetable, fdarray, (char *)&fd0, sizeof(fd0)) < 0 ||
      copyout(p->pagetable, fdarray + sizeof(fd0), (char *)&fd1, sizeof(fd1)) <
          0) {
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}
uint64 sys_connect(void) {
  struct file *f;
  int fd;
  uint32 raddr;
  uint32 rport;
  uint32 lport;
  argint(0, (int *)&raddr);
  argint(1, (int *)&lport);
  argint(2, (int *)&rport);
  if (raddr < 0 || lport < 0 || rport < 0) {
    return -1;
  }

  if (sockalloc(&f, raddr, lport, rport) < 0) return -1;
  if ((fd = fdalloc(f)) < 0) {
    fileclose(f);
    return -1;
  }

  return fd;
}

uint64 sys_mmap(void) {
  int len;
  int prot, flags, fd;
  struct file *f;
  argint(1, &len);
  argint(2, &prot);
  argint(3, &flags);
  argfd(4, &fd, &f);
  if (len < 0 || prot < 0 || flags < 0 || fd < 0) return -1;

  // if file is read-only,but map it as writable.return fail
  if (!f->writable && (prot & PROT_WRITE) && (flags & MAP_SHARED)) {
    return -1;
  }

  struct proc *p = myproc();
  for (uint i = 0; i < MAXVMA; i++) {
    struct VMA *v = &p->vma[i];
    if (!v->used)  // find an unsed vma
    {
      // store relative auguments
      v->used = 1;
      v->addr = p->sz;  // use p->sz to p->sz+len to map the file
      len = PGROUNDUP(len);
      p->sz += len;
      v->len = len;
      v->prot = prot;
      v->flags = flags;
      v->f = filedup(f);   // increase the file's ref cnt
      v->start_point = 0;  // staring point in f to map is 0
      return v->addr;
    }
  }

  return -1;
}

int file_write_new(struct file *f, uint64 addr,int n ,uint off)
{
   int r=0;
   if(f->writable==0) return -1;

   int max= ((MAXOPBLOCKS-1-1-2) / 2)* BSIZE;
   int i=0;
   while(i<n)
   {
      int n1=n-i;
      if(n1>max) n1=max;

      begin_op();
      ilock(f->ip);
      if((r=writei(f->ip , 1 , addr +i,off,n1)) >0 )
          off+=r;
      iunlock(f->ip);
      end_op();

      if(r!=n1)  break;
      i+=r;
   }

   return 0;
}

uint64 sys_munmap(void) {
  uint64 addr;
  int len;
  int close = 0;
  argaddr(0, &addr);
  argint(1, &len);
  if (addr == 0 || len < 0) return -1;
  struct proc *p = myproc();
  for (uint i = 0; i < MAXVMA; i++) {
    struct VMA *v = &p->vma[i];
    // only unmap at start,end or the whole region
    if (v->used && addr >= v->addr && addr <= v->addr + v->len) {
      uint64 npages = 0;
      uint off = v->start_point;
      if (addr == v->addr)  // unmap at start
      {
        if (len >= v->len)  // unmap whole region
        {
          len = v->len;
          v->used = 0;
          close = 1;
        } else  // unmap from start but not whole region
        {
          v->addr += len;
          v->start_point = len;  // update start point at which to map
        }
      }
      len = PGROUNDUP(len);
      npages = len / PGSIZE;
      v->len -= len;
      p->sz -= len;

      if (v->flags & MAP_SHARED)  // need to write back pages
      {
        file_write_new(v->f, addr, len, off);
      }

      uvmunmap(p->pagetable, PGROUNDDOWN(addr), npages, 0);
      // decrease ref cnt of v->f
      if (close) fileclose(v->f);

      return 0;
    }
  }
  return -1;
}