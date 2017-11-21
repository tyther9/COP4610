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
     if(flags < 0 || flags > allflags) { 
     return EINVAL; }

     if(upath == NULL) { 
     return EINVAL; }

     kpath  = (char *) kmalloc(sizeof(char)*PATH_MAX);
     // copy over path to kernel
     result = copyinstr(upath, kpath, sizeof(upath), NULL);

     /* open a file (args must be kernel pointers; destroys filename) */
     result = openfile_open(kpath, flags, mode, &file);
     if(result) { 
     return result; }

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

     *retval = size - theuio.uio_resid;

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

     // errors
     if(result) { return result; }
     //if (thefile->of_accmode == O_WRONLY) { return EBADF; }

     // initialize the kernel buffer
     kbuf = kmalloc(sizeof(*buf)*size);
     if(kbuf == NULL) { return EINVAL; }

     lock_acquire(thefile->of_offsetlock);

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

     *retval = size - theuio.uio_resid;

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
     if(result)
     {
          return result;
     }

     if(thefile->of_refcount == 1) 
     {
          // kfree(&thefile);
          filetable_placeat(curproc->p_filetable, NULL, fd, &thefile);
     } 
     else 
     {
          openfile_decref(thefile);
     }

     *retval = 0;
     return 0;
}


/* 
* meld () - combine the content of two files word by word into a new file
*/
