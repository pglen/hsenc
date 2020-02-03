// The actual file operations

#pragma GCC diagnostic ignored "-Wformat-truncation"

static inline struct xmp_dirp *get_dirp(struct fuse_file_info *fi)
{
	return (struct xmp_dirp *) (uintptr_t) fi->fh;
}

static int xmp_getattr(const char *path, struct stat *stbuf)
{
	int res;

    char  path2[PATH_MAX] ;
    strcpy(path2, mountsecret); strcat(path2, path);

	res = lstat(path2, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_fgetattr(const char *path, struct stat *stbuf,
			struct fuse_file_info *fi)
{
	int res;

	(void) path;

	res = fstat(fi->fh, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_access(const char *path, int mask)
{
	int res;

    char  path2[PATH_MAX] ;
    strcpy(path2, mountsecret); strcat(path2, path);

    if (loglevel > 3)
        syslog(LOG_DEBUG, "Get access, file: %s uid: %d\n", path, getuid());

	res = access(path2, mask);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_readlink(const char *path, char *buf, size_t size)
{
	int res;

	char  path2[PATH_MAX] ;
    strcpy(path2, mountsecret); strcat(path2, path);

    res = readlink(path2, buf, size - 1);
	if (res == -1)
		return -errno;

	buf[res] = '\0';
	return 0;
}

struct xmp_dirp {
	DIR *dp;
	struct dirent *entry;
	off_t offset;
};

static int xmp_opendir(const char *path, struct fuse_file_info *fi)
{
	int res;
	struct xmp_dirp *d = malloc(sizeof(struct xmp_dirp));
	if (d == NULL)
		return -ENOMEM;

    char  path2[PATH_MAX] ;
    strcpy(path2, mountsecret); strcat(path2, path);

	d->dp = opendir(path2);
	if (d->dp == NULL) {
		res = -errno;
		free(d);
		return res;
	}
	d->offset = 0;
	d->entry = NULL;

	fi->fh = (unsigned long) d;
	return 0;
}


static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
	struct xmp_dirp *d = get_dirp(fi);

	(void) path;
	if (offset != d->offset) {
		seekdir(d->dp, offset);
		d->entry = NULL;
		d->offset = offset;
	}
	while (1) {
		struct stat st;
		off_t nextoff;

		if (!d->entry) {
			d->entry = readdir(d->dp);
			if (!d->entry)
				break;
		}

		memset(&st, 0, sizeof(st));
		st.st_ino = d->entry->d_ino;
		st.st_mode = d->entry->d_type << 12;
		nextoff = telldir(d->dp);

		if (filler(buf, d->entry->d_name, &st, nextoff))
			break;

		d->entry = NULL;
		d->offset = nextoff;
	}

	return 0;
}

static int xmp_releasedir(const char *path, struct fuse_file_info *fi)
{
	struct xmp_dirp *d = get_dirp(fi);
	(void) path;

	closedir(d->dp);
	free(d);
	return 0;
}

static int xmp_mknod(const char *path, mode_t mode, dev_t rdev)
{
	int res;

    char  path2[PATH_MAX] ;
    strcpy(path2, mountsecret); strcat(path2, path);


	if (S_ISFIFO(mode))
		res = mkfifo(path2, mode);
	else
		res = mknod(path2, mode, rdev);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_mkdir(const char *path, mode_t mode)
{
	int res;

    char  path2[PATH_MAX] ;
    strcpy(path2, mountsecret); strcat(path2, path);

    if (loglevel > 3)
        syslog(LOG_DEBUG, "Mkdir dir: %s uid: %d\n", path, getuid());

	res = mkdir(path2, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_unlink(const char *path)
{
	int res;

    char  path2[PATH_MAX] ;
    strcpy(path2, mountsecret); strcat(path2, path);

    if (loglevel > 3)
        syslog(LOG_DEBUG, "Unlinked file: %s uid: %d\n", path, getuid());

	res = unlink(path2);
	if (res == -1)
		return -errno;

    // Also unlink the .secret file
    // Reassemble with dot path

    char *ptmp2 = malloc(PATH_MAX);
    if(ptmp2)
        {
        strcpy(ptmp2, mountsecret);
        char *endd = strrchr(path, '/');
        if(endd)
            {
            strncat(ptmp2, path, endd - path);
            strcat(ptmp2, ".");
            strcat(ptmp2, endd + 1);
            }
        else
            {
            strcat(ptmp2, ".");
            }
        strcat(ptmp2, ".secret");
        if (loglevel > 3)
            syslog(LOG_DEBUG, "Unlinked secret file: %s\n", ptmp2);
        unlink(ptmp2);
        }
	return 0;
}

static int xmp_rmdir(const char *path)
{
	int res;

    char  path2[PATH_MAX] ;
    strcpy(path2, mountsecret); strcat(path2, path);

    if (loglevel > 3)
        syslog(LOG_DEBUG, "Removed dir: %s uid: %d\n", path, getuid());

	res = rmdir(path2);
	if (res == -1)
		return -errno;

	return 0;
}

//# Symlink is not implemented in the encrypted file system
// We disabled symlink, as it confused the dataroot. Remember we link
// to dataroot as an intercept.

static int xmp_symlink(const char *from, const char *to)
{
	int res;

    return -ENOSYS;

    char  path2[PATH_MAX] ;
    strcpy(path2, mountsecret); strcat(path2, from);

    char  path3[PATH_MAX] ;
    strcpy(path3, mountsecret); strcat(path3, to);

    //if (loglevel > 1)
    //    syslog(LOG_DEBUG, "Symlink file: %s -> %s\n", path, path3);

	res = symlink(path2, path3);
	//res = symlink(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_rename(const char *from, const char *to)
{
	int res;

    char  path2[PATH_MAX] ;
    strcpy(path2, mountsecret); strcat(path2, from);

    char  path3[PATH_MAX] ;
    strcpy(path3, mountsecret); strcat(path3, to);

    if (loglevel > 3)
        syslog(LOG_DEBUG, "Renamed file: %s to %s uid: %d\n", from, to, getuid());

	res = rename(path2, path3);
	//res = rename(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

// We disabled link, as it confused the dataroot. Remember we link to dataroot
// as an intercept.

static int xmp_link(const char *from, const char *to)
{
	int res;

    return -ENOSYS;

    char  path2[PATH_MAX] ;
    strcpy(path2, mountsecret); strcat(path2, from);

    char  path3[PATH_MAX] ;
    strcpy(path3, mountsecret); strcat(path3, to);

	//res = link(path2, path3);
	res = link(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_chmod(const char *path, mode_t mode)
{
	int res;

    char  path2[PATH_MAX] ;
    strcpy(path2, mountsecret); strcat(path2, path);

    if (loglevel > 3)
        syslog(LOG_DEBUG, "Chmod file: %s uid: %d mode: %d\n", path, getuid(), mode);

	res = chmod(path2, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_chown(const char *path, uid_t uid, gid_t gid)
{
	int res;

    char  path2[PATH_MAX] ;
    strcpy(path2, mountsecret); strcat(path2, path);

    if (loglevel > 3)
        syslog(LOG_DEBUG, "Chown file: %s uid: %d touid: %d togid %d\n",
                path, getuid(), uid, gid);

	res = lchown(path2, uid, gid);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_truncate(const char *path, off_t size)
{
	int res;

    char  path2[PATH_MAX] ;
    strcpy(path2, mountsecret); strcat(path2, path);

    if (loglevel > 3)
        syslog(LOG_DEBUG, "Truncated file: %s uid: %d\n", path, getuid());

    // Kill sideblock too
    kill_sideblock(path2);

	res = truncate(path2, size);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_ftruncate(const char *path, off_t size,
			 struct fuse_file_info *fi)
{
	int res;

	(void) path;

    if (loglevel > 3)
        syslog(LOG_DEBUG, "Truncated file: %s uid: %d\n", path, getuid());

	res = ftruncate(fi->fh, size);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_utimens(const char *path, const struct timespec ts[2])
{
	int res;
	struct timeval tv[2];

	tv[0].tv_sec = ts[0].tv_sec;
	tv[0].tv_usec = ts[0].tv_nsec / 1000;
	tv[1].tv_sec = ts[1].tv_sec;
	tv[1].tv_usec = ts[1].tv_nsec / 1000;

    char  path2[PATH_MAX] ;
    strcpy(path2, mountsecret); strcat(path2, path);

	res = utimes(path2, tv);
	if (res == -1)
		return -errno;

	return 0;
}

int     openpass(const char *path)

{
    char tmp[MAXPASSLEN];
    int ret = 0;

    if(passprog[0] == 0)
        {
        if (loglevel > 1)
            syslog(LOG_DEBUG, "No pass program specified: %s uid: %d\n", path, getuid());
        return 1;
        }
    char *res = hs_askpass(passprog, tmp, MAXPASSLEN);
    if (res == NULL || strlen(res) == 0)
        {
        if (loglevel > 1)
            syslog(LOG_DEBUG, "Cannot get pass for %s uid: %d\n", path, getuid());
        return 1;
        }

    strncpy(passx, res, sizeof(passx));

    int ret2 = pass_ritual(mountpoint, mountsecret, passx, &plen);
    if(ret2)
        {
        // Force new pass prompt
        memset(passx, 0, sizeof(passx));
        if (loglevel > 1)
            syslog(LOG_DEBUG, "Invalid pass for %s uid: %d\n", path, getuid());
        return ret2;
        }
    return ret;
}

static int xmp_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	int fd;

    char  path2[PATH_MAX] ;
    strcpy(path2, mountsecret); strcat(path2, path);

    if (loglevel > 1)
        syslog(LOG_DEBUG, "Created file: '%s' uid: %d\n", path, getuid());

    if (loglevel > 2)
        syslog(LOG_DEBUG, "Shadow file: '%s'\n", path2);

    if(passx[0] == 0)
        {
        if (loglevel > 3)
            syslog(LOG_DEBUG, "Empty pass on create file: %s uid: %d\n", path, getuid());
        int ret = openpass(path);
        if (ret)
            return -EACCES;
        }
	fd = open(path2, fi->flags, mode);
	if (fd == -1)
		return -errno;
	fi->fh = fd;

    struct stat stbuf;	memset(&stbuf, 0, sizeof(stbuf));
    int res = fstat(fi->fh, &stbuf);
    if(res < 0)
        {
        if (loglevel > 2)
            syslog(LOG_DEBUG, "Cannot stat newly created file '%s'\n", path);

        goto endd;
        }

    //if (loglevel > 2)
    //    syslog(LOG_DEBUG, "Inode: %lud blocksize %ld \n",
    //                                stbuf.st_ino, stbuf.st_blksize);

    char *ptmp2 = get_sidename(path);
    if(ptmp2)
        {
        if (loglevel > 2)
            syslog(LOG_DEBUG, "Creating '%s'\n", ptmp2);

        int old_errno = errno;
        int fdi = open(ptmp2, O_RDWR | O_CREAT | O_TRUNC , S_IRUSR | S_IWUSR);
        if(fdi < 0)
            {
            if (loglevel > 2)
                syslog(LOG_DEBUG, "Error on creating '%s' errno: %d\n", ptmp2, errno);
            }
        else
            {
            char *ptmp3 = malloc(stbuf.st_blksize);
            if(ptmp3)
                {
                memset(ptmp3, '\0', stbuf.st_blksize);
                int ww = write(fdi, ptmp3, stbuf.st_blksize);
                if(ww < stbuf.st_blksize)
                    {
                    if (loglevel > 2)
                        syslog(LOG_DEBUG, "Error on writing to inode file errno: %d\n", errno);
                    }
                free(ptmp3);
                }
            close(fdi);
            }
        errno = old_errno;
        free(ptmp2);
        }

    endd:
        ;
	return 0;
}

static int xmp_open(const char *path, struct fuse_file_info *fi)
{
	int fd;

    char  path2[PATH_MAX] ;
    strcpy(path2, mountsecret); strcat(path2, path);

    //if (loglevel > 1)
    //    syslog(LOG_DEBUG, "Opened file: %s uid: %d\n", path, getuid());

    if(passx[0] == 0)
        {
        if (loglevel > 3)
            syslog(LOG_DEBUG, "Empty pass on open file: %s uid: %d\n", path, getuid());
        int ret = openpass(path);
        if (ret)
            return -EACCES;
        }
	fd = open(path2, fi->flags);
	if (fd == -1)
		return -errno;
	fi->fh = fd;

    struct stat stbuf;	memset(&stbuf, 0, sizeof(stbuf));
    int res = fstat(fi->fh, &stbuf);

    //if (loglevel > 2)
    //    syslog(LOG_DEBUG, "Inode: %lud\n", stbuf.st_ino);

	return 0;
}

static int xmp_statfs(const char *path, struct statvfs *stbuf)
{
	int res;

    char  path2[PATH_MAX] ;
    strcpy(path2, mountsecret); strcat(path2, path);

    if (loglevel > 3)
        syslog(LOG_DEBUG, "Stat file: %s uid: %d\n", path, getuid());

	res = statvfs(path2, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_flush(const char *path, struct fuse_file_info *fi)
{
	int res;

    //if (loglevel > 3)
    //    syslog(LOG_DEBUG, "Flushed file: %s uid: %d\n", path, getuid());

	(void) path;
	/* This is called from every close on an open file, so call the
	   close on the underlying filesystem.	But since flush may be
	   called multiple times for an open file, this must not really
	   close the file.  This is important if used on a network
	   filesystem like NFS which flush the data/metadata on close() */

	res = close(dup(fi->fh));

	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_release(const char *path, struct fuse_file_info *fi)
{

    //if (loglevel > 3)
    //    syslog(LOG_DEBUG, "Released file: %s uid: %d\n", path, getuid());

	(void) path;
	close(fi->fh);

	return 0;
}

static int xmp_fsync(const char *path, int isdatasync,
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

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int xmp_setxattr(const char *path, const char *name, const char *value,
			size_t size, int flags)
{
    char  path2[PATH_MAX] ;
    strcpy(path2, mountsecret); strcat(path2, path);

	int res = lsetxattr(path2, name, value, size, flags);
	if (res == -1)
		return -errno;
	return 0;
}

static int xmp_getxattr(const char *path, const char *name, char *value,
			size_t size)
{
    char  path2[PATH_MAX] ;
    strcpy(path2, mountsecret); strcat(path2, path);

	int res = lgetxattr(path2, name, value, size);
	if (res == -1)
		return -errno;
	return res;
}

static int xmp_listxattr(const char *path, char *list, size_t size)
{
    char  path2[PATH_MAX] ;
    strcpy(path2, mountsecret); strcat(path2, path);

	int res = llistxattr(path3, list, size);
	if (res == -1)
		return -errno;
	return res;
}

static int xmp_removexattr(const char *path, const char *name)
{
    char  path2[PATH_MAX] ;
    strcpy(path2, mountsecret); strcat(path2, path);

	int res = lremovexattr(path2, name);
	if (res == -1)
		return -errno;
	return 0;
}
#endif /* HAVE_SETXATTR */

static int xmp_lock(const char *path, struct fuse_file_info *fi, int cmd,
		    struct flock *lock)
{
	(void) path;

	return ulockmgr_op(fi->fh, cmd, lock, &fi->lock_owner,
			   sizeof(fi->lock_owner));
}















