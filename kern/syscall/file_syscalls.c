/*
 * File-related system call implementations.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/seek.h>
#include <kern/stat.h>
#include <lib.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <copyinout.h>
#include <vfs.h>
#include <vnode.h>
#include <openfile.h>
#include <filetable.h>
#include <syscall.h>

/*
 * open() - get the path with copyinstr, then use openfile_open and
 * filetable_place to do the real work.
 */
int
sys_open(const_userptr_t upath, int flags, mode_t mode, int *retval)
{
	const int allflags = O_ACCMODE | O_CREAT | O_EXCL | O_TRUNC | O_APPEND | O_NOCTTY;

	char *kpath;
	struct openfile *file;
	int result = 0;

     // Validate flags
     if(flags < 0 || flags > allflags) { return EINVAL; }

     if(upath == NULL) { return EINVAL; }

     kpath  = (char *) kmalloc(sizeof(char)*PATH_MAX);
     // copy over path to kernel, plus 1 to work around an issue with copyintostr
     result = copyinstr(upath, kpath, strlen(((char *)upath)) + 1, NULL);
     if(result) { return result; }

     /* open a file (args must be kernel pointers; destroys filename) */
     result = openfile_open(kpath, flags, mode, &file);
     kfree(kpath);
     if(result) { return result; }

     return filetable_place(curproc->p_filetable, file, retval);
}

/*
 * read() - read data from a file
 */
int
sys_read(int fd, userptr_t buf, size_t size, int *retval)
{
     int result = 0;
     struct openfile *thefile;
     struct uio theuio;
     struct iovec iov;
     void *kbuf;     
     
     result = filetable_get(curproc->p_filetable, fd, &thefile);

     // errors
     if(result) { return result; }
     if (thefile->of_accmode == O_WRONLY) { return EBADF; }

     // initialize the kernel buffer
     kbuf = kmalloc(sizeof(*buf)*size);
     if(kbuf == NULL) { return EINVAL; }

     lock_acquire(thefile->of_offsetlock);
     int oldOffset = thefile->of_offset;

     iov.iov_ubase = (userptr_t)buf;
     iov.iov_len = size;
     theuio.uio_iov = &iov;
     theuio.uio_iovcnt = 1;
     theuio.uio_offset = thefile->of_offset;
     theuio.uio_resid = size;
     theuio.uio_segflg = UIO_USERSPACE;
     theuio.uio_rw = UIO_READ;
     theuio.uio_space = curproc->p_addrspace;

     result = VOP_READ(thefile->of_vnode, &theuio);

     if(result) 
     {
          kfree(kbuf);
          lock_release(thefile->of_offsetlock);
          return result;
     }

     // number of bytes read
     *retval = theuio.uio_offset - oldOffset;

     thefile->of_offset = theuio.uio_offset;

     kfree(kbuf);
     lock_release(thefile->of_offsetlock);

     filetable_put(curproc->p_filetable, fd, thefile);

     return result;
}

/*
 * write() - write data to a file
 */
int
sys_write(int fd, userptr_t buf, size_t size, int *retval)
{
     int result = 0;
     struct openfile *thefile;
     struct uio theuio;
     struct iovec iov;
     void *kbuf;     
     
     result = filetable_get(curproc->p_filetable, fd, &thefile);
     if(result) { return result; }

     // initialize the kernel buffer
     kbuf = kmalloc(sizeof(*buf)*size);
     if(kbuf == NULL) { return EINVAL; }

     lock_acquire(thefile->of_offsetlock);

     int oldOffset = thefile->of_offset;

     iov.iov_ubase = (userptr_t)buf;
     iov.iov_len = size;
     theuio.uio_iov = &iov;
     theuio.uio_iovcnt = 1;
     theuio.uio_offset = thefile->of_offset;
     theuio.uio_resid = size;
     theuio.uio_segflg = UIO_USERSPACE;
     theuio.uio_rw = UIO_WRITE;
     theuio.uio_space = curproc->p_addrspace;

     result = VOP_WRITE(thefile->of_vnode, &theuio);

     if(result) 
     {
          kfree(kbuf);
          lock_release(thefile->of_offsetlock);
          return result;
     }

     // number of bytes written
     *retval = theuio.uio_offset - oldOffset;
     thefile->of_offset = theuio.uio_offset;

     kfree(kbuf);
     lock_release(thefile->of_offsetlock);

     filetable_put(curproc->p_filetable, fd, thefile);

     return result;
}
/*
 * close() - remove from the file table.
 */
int
sys_close(int fd, int *retval) 
{
     int result = 0;
     struct openfile *thefile;

     result = filetable_get(curproc->p_filetable, fd, &thefile);
     if(result) { return result;}

     if(thefile->of_refcount == 1) 
     {
          filetable_placeat(curproc->p_filetable, NULL, fd, &thefile);
     } 

     openfile_decref(thefile);

     *retval = 0;
     return 0;
}

/*
 * meld() 
 */
int
sys_meld(const_userptr_t upath1, const_userptr_t upath2, const_userptr_t upathmerge, int *retval)
{
     char *kpath1, *kpath2, *kpathmerge;
     struct openfile *file1, *file2, *filemerge;
     struct uio read1uio, read2uio, writeuio;
     struct iovec iov;
     int result = 0;
     void *kbuf1,*kbuf2; 

     // paths not null
     if(upath1 == NULL || upath2 == NULL || upathmerge == NULL) { return EINVAL; }

     // convert to kernel
     kpath1 = (char *) kmalloc(sizeof(char) * PATH_MAX);
     kpath2 = (char *) kmalloc(sizeof(char) * PATH_MAX);
     kpathmerge = (char *) kmalloc(sizeof(char) * PATH_MAX);

     // copy over path to kernel
     result = copyinstr(upath1, kpath1, strlen(((char *)upath1)) + 1, NULL);
     if(result) { return result; }

     result = copyinstr(upath2, kpath2, strlen(((char *)upath2)) + 1, NULL);
     if(result) { return result; }

     result = copyinstr(upathmerge, kpathmerge, strlen(((char *)upathmerge)) + 1, NULL);
     if(result) { return result; }

     /* open the file (args must be kernel pointers; destroys filename) */
     result = openfile_open(kpath1, O_RDONLY, 055, &file1);
     if(result) { return result; }

     result = openfile_open(kpath2, O_RDONLY, 055, &file2);
     if(result) { return result; }

     result = openfile_open(kpathmerge, O_WRONLY|O_CREAT|O_EXCL, 0664, &filemerge);
     if(result) { return result; }

     kbuf1 = kmalloc(sizeof(char) * 4); // four bytes
     kbuf2 = kmalloc(sizeof(char) * 4); // four bytes

     // loop assistants
     int file1LastOffset = 0, file2LastOffset = 0;
     int loopcnt = 0, file1Read = -1, file2Read = -1;

     while(file1Read != 0 && file2Read != 0)
     {
         // read from file 1
         lock_acquire(file1->of_offsetlock);

         uio_kinit(&iov, &read1uio, kbuf1, 4, file1->of_offset, UIO_READ);
         result = VOP_READ(file1->of_vnode, &read1uio);
         file1Read = read1uio.uio_offset - file1LastOffset;
         file1->of_offset = read1uio.uio_offset;
         file1LastOffset = read1uio.uio_offset;

         lock_release(file1->of_offsetlock);

         // Read from file 2
         lock_acquire(file2->of_offsetlock);

         uio_kinit(&iov, &read2uio, kbuf2, 4, file2->of_offset, UIO_READ);
         result = VOP_READ(file2->of_vnode, &read2uio); 
         file2Read = read2uio.uio_offset - file2LastOffset;
         file2->of_offset = read2uio.uio_offset;
         file2LastOffset = file2->of_offset;

         lock_release(file2->of_offsetlock);

         if((file1Read > 0 && file1Read < 4) || (file2Read > 0 && file1Read == 0))
         {
               char * ptr = (char *)kbuf1;
               int start = file1Read;
               if(file1Read == 0) { start = 1; }
               for(loopcnt = start - 1; loopcnt < 4; loopcnt ++)
               {
                    ptr[loopcnt] = ' ';
               }
         }

         if((file2Read > 0 && file2Read < 4) || (file1Read > 0 && file2Read == 0))
         {
               char * ptr = (char *)kbuf2;
               int start = file2Read;
               if(file2Read == 0) { start = 1; }
               for(loopcnt = start - 1; loopcnt < 4; loopcnt ++)
               {
                    ptr[loopcnt] = ' ';
               }
         }

         if(file1Read != 0 || file2Read != 0)
         {
              // write the bytes to the new file
              lock_acquire(filemerge->of_offsetlock);
              uio_kinit(&iov, &writeuio, kbuf1, 4, filemerge->of_offset, UIO_WRITE);
              result = VOP_WRITE(filemerge->of_vnode, &writeuio);
              filemerge->of_offset = writeuio.uio_offset;
              lock_release(filemerge->of_offsetlock);

              // write to the new file
              lock_acquire(filemerge->of_offsetlock);
              uio_kinit(&iov, &writeuio, kbuf2, 4, filemerge->of_offset, UIO_WRITE);
              result = VOP_WRITE(filemerge->of_vnode, &writeuio);
              filemerge->of_offset = writeuio.uio_offset;
              lock_release(filemerge->of_offsetlock);
         }
     }

     // number of bytes written
     *retval = filemerge->of_offset;

     openfile_decref(file1);
     openfile_decref(file2);
     openfile_decref(filemerge);

     kfree(kpath1);
     kfree(kpath2);
     kfree(kpathmerge);
     kfree(kbuf1);
     kfree(kbuf2);

     return 0;
}
