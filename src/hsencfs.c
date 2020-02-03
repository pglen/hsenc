/*
 * HSENCFS (High Security EnCrypting File System)
 *
 * High security encryption file system. We make use of the API offered by
 * the fuse subsystem to intercept file operations.
 *
 * The interception is done between mountsecret and mountpoint. Copying data
 * to mountpoint ends up encrypted in mountsecret. Copying data from mountpoint
 * is sourced from mountsecret and decrypted. See "hsencrw.c".
 *
 * Use a dotted file for mountsecret (like .data or .secretdata)
 *
 * One additional useful feature is auditing. Reports file access by user ID.
 * The report is sent to syslog. (use the -l option to turn on log)
 *
 * To make it:
 *
 *     gcc -lfuse -lulockmgr [localobjects ...] hsencfs.c -o hsencfs
 *
 *  To use it:
 *
 *       hsenc [-f] ~/secrets  ~/.secrets
 *
 *  Command above will expose the ~/secrets directory. It is sourced from the
 *  backing directory ~/.secrets
 */

#define FUSE_USE_VERSION 26

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE

#include <fuse.h>
#include <ulockmgr.h>

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <syslog.h>
#include <sys/time.h>

#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

#include <signal.h>
#include <getopt.h>

#include "hsutils.h"
#include "base64.h"
#include "../bluepoint/bluepoint2.h"

#define MAXPASSLEN 512

// Log
static FILE *logfp = NULL;

// Flags
static  int     verbose = 0;
static  int     quiet = 0;
static  int     force = 0;
static  int     ondemand = 0;

// Shared flags
int     loglevel = 0;

// Maintain internal count
static  char    version[] = "1.18";

// The decoy employed occasionally to stop spyers
// from figuring out where it is stored

static  char    passx[MAXPASSLEN];
static  int     plen = sizeof(passx);
static  char    decoy[MAXPASSLEN];
static  int     plen2 = sizeof(decoy);

// Main directories for data / encryption

static  char  mountpoint[PATH_MAX] ;
static  char  mountsecret[PATH_MAX] ;
static  char  tmpsecret[PATH_MAX] ;
static  char  passprog[PATH_MAX] ;

static  char  inodedir[PATH_MAX] ;

/// We use this as a string to obfuscate the password. Do not change.
char    progname[] = "HSENCFS";
int     pg_debug = 0;

char *get_sidename(const char *path)

{
    char *ptmp2 = malloc(PATH_MAX);
    if(ptmp2)
        {
        // Reassemble with dot path
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
        }
    return ptmp2;
}

// -----------------------------------------------------------------------
// Get the extracted sources:

#include "hsencsb.c"
#include "hsencrr.c"                // Separated to read / write
#include "hsencrw.c"
#include "hsencop.c"

static struct fuse_operations xmp_oper = {
	.getattr	= xmp_getattr,
	.fgetattr	= xmp_fgetattr,
	.access		= xmp_access,
	.readlink	= xmp_readlink,
	.opendir	= xmp_opendir,
	.readdir	= xmp_readdir,
	.releasedir	= xmp_releasedir,
	.mknod		= xmp_mknod,
	.mkdir		= xmp_mkdir,
	.symlink	= xmp_symlink,
	.unlink		= xmp_unlink,
	.rmdir		= xmp_rmdir,
	.rename		= xmp_rename,
	.link		= xmp_link,
	.chmod		= xmp_chmod,
	.chown		= xmp_chown,
	.truncate	= xmp_truncate,
	.ftruncate	= xmp_ftruncate,
	.utimens	= xmp_utimens,
	.create		= xmp_create,
	.open		= xmp_open,
	.read		= xmp_read,
	.write		= xmp_write,
	.statfs		= xmp_statfs,
	.flush		= xmp_flush,
	.release	= xmp_release,
	.fsync		= xmp_fsync,
#ifdef HAVE_SETXATTR
	.setxattr	= xmp_setxattr,
	.getxattr	= xmp_getxattr,
	.listxattr	= xmp_listxattr,
	.removexattr	= xmp_removexattr,
#endif
	.lock		= xmp_lock,

	.flag_nullpath_ok = 1,
};

// Use /proc/self/fd directory to close fd-s

void    closefrom(int lowfd)

{
    struct dirent *dent;  DIR *dirp;
    char *endp;  long fd;

    if ((dirp = opendir("/proc/self/fd")) != NULL) {
	   while ((dent = readdir(dirp)) != NULL) {
	   fd = strtol(dent->d_name, &endp, 10);
	    if (dent->d_name != endp && *endp == '\0' &&
		  fd >= 0 && fd < INT_MAX && fd >= lowfd && fd != dirfd(dirp))
            {
            (void) close((int) fd);
            }
	   }
	   (void) closedir(dirp);
    }
}

// Simple help

int help()

{
    printf("\n");
    printf("Usage: hsencfs [options] MountPoint [StorageDir] \n");
    printf("\n");
    printf("MountPoint is a directory for user visible data.\n");
    printf("StorageDir is a storage directory for encrypted data.\n");
    printf("Use dotted name as 'storagedir' for convenient hiding of data file names.\n");
    printf("Options:        -v       -- Verbose. (--verbose)\n");
    printf("                -p pass  -- Use pass. Note: cleartext pass.(--pass)\n");
    printf("                -a prog  -- Use program to askin pass. (--askpass)\n");
    printf("                -o       -- On demand pass. Ask on first access. (--ondemand)\n");
    printf("                -f       -- Force creation of storagedir/mountpoint. (--force)\n");
    printf("                -q       -- Quiet, minimal diagnostics printed (--quiet)\n");
    printf("                -V       -- Print hsencfs version. (--version)\n");
    printf("                -d level -- Debug level 0 ... 10 Default: 0 (--debug)\n");
    printf("                -l level -- Log to syslog. Default: None (--loglevel)\n");
    printf("Log levels:      1 - start/stop;   2 - open/create     3 - read/write;\n");
    printf("                 4 - all (noisy);  5 - show internals \n\n");
    printf("Use '--' to at the end of options for appending fuse options.\n");
    printf("For example: 'hsencfs secretdata mountpoint -- -o ro' for read only mount.\n");
    printf("Typical invocation: (note the leading dot)\n");
    printf("    hsencfs  ~/secrets ~/.secrets\n\n");

}

// -----------------------------------------------------------------------
// Read a line from the forked program

static char *getln(int fd, char *buf, size_t bufsiz)

{
    size_t left = bufsiz;
    ssize_t nr = -1;
    char *cp = buf; char c = '\0';

    if (left == 0) {
    	errno = EINVAL;
    	return(NULL);			/* sanity */
    }

    while (--left) {
	nr = read(fd, &c, 1);
	if (nr != 1 || c == '\n' || c == '\r')
	    break;
	*cp++ = c;
    }
    *cp = '\0';

    return(nr == 1 ? buf : NULL);
}

/*
 * Fork a child and exec progran to get the password from the user.
 */

char *hs_askpass(const char *program, char *buf, int buflen)

{
    struct sigaction  sa, saved_sa_pipe;
    int pfd[2];   pid_t pid;

    if (pipe(pfd) == -1)
	   perror("Unable to create pipe");

	   //perror(1, "Unable to create pipe");

    if ((pid = fork()) == -1)
	   perror("Unable to fork");

        //error(1, "Unable to fork");

    if (pid == 0) {
    	/* child, point stdout to output side of the pipe and exec askpass */
    	if (dup2(pfd[1], STDOUT_FILENO) == -1) {
    	    printf("Unable to dup2");
    	    _exit(255);
    	   }
    	(void) dup2(pfd[1], STDOUT_FILENO);
        // Redirect error messages:
        int fh = open("/dev/null", O_RDWR );
    	(void) dup2(fh, STDERR_FILENO);

    	//set_perms(PERM_FULL_USER); //TODO
    	closefrom(STDERR_FILENO + 1);
    	execl(program, program, NULL);
    	//execl(program, (char *)NULL);
    	printf("Unable to run %s", program);
    	_exit(255);
        }

    /* Ignore SIGPIPE in case child exits prematurely */
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_INTERRUPT;
    sa.sa_handler = SIG_IGN;
    (void) sigaction(SIGPIPE, &sa, &saved_sa_pipe);

    /* Get response from child and restore SIGPIPE handler */
    (void) close(pfd[1]);

    char *xpass = getln(pfd[0], buf, buflen);
    (void) close(pfd[0]);
    (void) sigaction(SIGPIPE, &saved_sa_pipe, NULL);

    // Decode base64
    return(xpass);
}

// -----------------------------------------------------------------------
// Process command line options, set up for mount.

static struct option long_options[] =
    {
        {"loglevel",    1,  0,  'l'},
        {"help",        0,  0,  'h'},
        {"pass",        1,  0,  'p'},
        {"quiet",       0,  0,  'g'},
        {"force",       0,  0,  'f'},
        {"verbose",     0,  0,  'v'},
        {"version",     0,  0,  'V'},
        {"askpass",     0,  0,  'a'},
        {"ondemand",    0,  0,  'o'},
        {0,             0,  0,   0}
    };

//////////////////////////////////////////////////////////////////////////

int     test_mountpoint(char *ppp, char *mpdir, char *msg)

{
    int ret = 0; struct stat sss;

    //printf("test_mountpoint() ppp='%s'\n", ppp);

    if(strlen(ppp) == 0)
        {
        fprintf(stderr,"Must specify %s directory.\n", msg);
        //help();
        exit(2);
        }

    // Resolve relative path
    expandpath(ppp, mpdir, PATH_MAX);

    //printf("test_mountpoint() mpdir='%s'\n", mpdir);

    if (mpdir[strlen(mpdir)-1] != '/')
        strcat(mpdir, "/");

    if (access(mpdir, R_OK) < 0)
        {
        if(!quiet)
            printf("Cannot access %s dir: '%s'\n", msg, mpdir);

        if (force)
            {
            if(verbose && !quiet)
                printf("Forcing directory creation.\n");

            if(!quiet)
                printf("Creating %s dir: '%s'\n", msg, mpdir);

            if (mkdir(mpdir, 0744) < 0)
                {
                fprintf(stderr,"Cannot create mount point dir: '%s'\n", mpdir);
                exit(3);
                }
            }
        else
            {
            exit(2);
            }

        stat(mpdir, &sss);
        if(!S_ISDIR(sss.st_mode))
            {
            fprintf(stderr,"%s must be a directory: '%s'\n", msg, mpdir);
            exit(2);
            }
        }
    return ret;
}

// ----------------------------------------------------------------------
// Parse command line

void    parse_comline(int argc, char *argv[])

{
    int cc, digit_optind = 0, loop, loop2;
    char *opts = "a:fhl:p:oqvVd:";
    //opterr = 0;

    while (1)
        {
        int this_option_optind = optind ? optind : 1;
        int option_index = -1;

    	cc = getopt_long(argc, argv, opts,
                         long_options, &option_index);

        if(pg_debug > 5)
            {
            printf("parse: cc=%c (%d) ", cc, cc);

            if (optarg)
                printf (" with arg '%s'", optarg);

            if(option_index >= 0)
                {
                printf ("\n   -- long option '%s' idx: %d val '%c' ",
                          long_options[option_index].name,
                                option_index, long_options[option_index].val);
                }
            printf("\n");
            }

        if (cc == -1)
            {
            //printf("option bailed\n");
            break;
            }

        int ret = 0, loop = 0;

        switch (cc)
           {
           case 'a':
                strncpy(passprog, optarg, MAXPASSLEN);
                if(verbose)
                    printf("Getting pass from program: '%s'\n", passprog);
                break;

           case 'd':
                pg_debug = atoi(optarg);
                if(verbose)
                    printf("Setting debug level: '%d'\n", pg_debug);
                break;

            case 'f':
                force = 1;
                break;

           case 'l':
               loglevel = atoi(optarg);
                if(verbose)
                    printf("Setting log Level: %d\n", loglevel);
               break;

           case 'p':
                if (passx[0] != 0)
                    {
                    fprintf(stderr, "%s Error: multiple passes on command line.\n", argv[0]);
                    exit(1);
                    }
                strncpy(passx, optarg, sizeof(passx));
                plen = strlen(passx);
                // Randomize optarg
                for(loop = 0; loop < plen; loop++)
                    {
                    ((char*)optarg)[loop] = rand() % 0x80;
                    }
                if(verbose)
                    printf("Pass provided on command line.\n");

                //if(pg_debug > 5)
                //    printf("Pass '%s' provided on command line.\n", passx);

                break;

           case 'q':
               quiet = 1;
               break;

           case 'o':
               ondemand = 1;
               break;

            case 'v':
               verbose = 1;
               break;

           case 'V':
               printf("%s Version: %s\n", argv[0], version);
               //printf("FUSE Version: %d\n", get_fuse_version);
               exit(0);
               break;

           case 'h':
                if(verbose)
                    printf("Showing help.\n");
               help(); exit(0);
               break;

           case '?':
                //fprintf(stderr, "%s Error: invalid option on command line.\n",
                //                    argv[0]);
                exit(1);
                break;

           default:
               printf ("?? getopt returned character code 0%o (%c) ??\n", cc, cc);

        }
    }
}

// -----------------------------------------------------------------------
// Main entry point

int     main(int argc, char *argv[])

{
    struct timespec ts;
    struct stat ss; char *msptr = NULL;

    clock_gettime(CLOCK_REALTIME, &ts);
    umask(0);

    memset(mountpoint,  0, sizeof(mountpoint));
    memset(mountsecret,   0, sizeof(mountsecret));
    memset(tmpsecret,   0, sizeof(tmpsecret));
    memset(passprog,    0, sizeof(passprog));
    memset(passx,       0, sizeof(passx));
    memset(decoy,       0, sizeof(decoy));

    // Just for development. DO NOT USE!
    //strcpy(passx, "1234"); //plen = strlen(passx);

    parse_comline(argc, argv);

    // Al least one arguments for md mp
    if (optind >= argc)
        {
        printf("Use: %s -h (or --help) for more information.\n", argv[0]);
		exit(1);
        }

    //printf("optind=%d argc=%d\n", optind, argc);

    // 1.) Test for mount point
    test_mountpoint(argv[optind], mountpoint, "mount point");

    // 2.) Test for optional mount secret
    if (optind <= argc - 2)
        {
        msptr =  argv[optind+1];
        }
    else
        {
        // Optional data path
        msptr = tmpsecret;
        int cnt = 0, cnt2 = 0; char *pch, *temp;
        //char *ptr1, *ptr2;

        // strtok needs different string in successive calls
        char *ddd = strdup(mountpoint);
        pch = strtok(ddd, "/");
        while ( (temp = strtok (NULL, "/") ) != NULL)
            cnt++;
        free(ddd);

        //printf("cnt %d\n", cnt);

        char *eee = strdup(mountpoint);
        pch = strtok(eee, "/");
        strcat(tmpsecret, "/"); strcat(tmpsecret, pch);
        //printf("tokenx '%s'\n", pch);

        while ( (temp = strtok(NULL, "/") ) != NULL)
          {
            cnt2++;
            //printf("token %d  '%s'\n", cnt2, temp);
            if(strcmp(temp, "."))
                {
                strcat(tmpsecret, "/");
                if(cnt2 == cnt)
                    strcat(tmpsecret, ".");
                strcat(tmpsecret, temp);
                }
            }
        free(eee);
        }

    //printf("msptr '%s'\n", msptr);
    test_mountpoint(msptr, mountsecret, "mount secret");
    //printf("mp '%s' sd '%s'\n",  mountpoint, mountsecret);

    // Make sure mroot and mdata exists, and are directories
    if (verbose && !quiet)
        {
        printf("Mount  Point dir: '%s'\n", mountpoint);
        printf("Mount Secret dir: '%s'\n", mountsecret);
        }

    // Make sure they are not nested:
    //   Note: these tests are not fool proof, added to TODO
    char *match  = strstr(mountpoint, mountsecret);
    if(match)
        {
        fprintf(stderr,"Mount Point must not be nested in Mount Data.\n");
        exit(6);
        }
    char *match2 = strstr(mountsecret, mountpoint);
     if(match2)
        {
        fprintf(stderr,"Mount Data must not be nested in Mount Point.\n");
        exit(6);
        }

    // --------------------------------------------------------------------
    // Primitive debug facility. Use tail -f hsenc.log to monitor this file
    // from a separate terminal. We mostly used the log facility.

    openlog("HSENCFS",  LOG_PID,  LOG_DAEMON);

    DIR *dd; struct dirent *dir;
    dd = opendir(mountpoint);
    if (!dd)
        {
        fprintf(stderr,"Cannnot open Mount Point directory\n");
        exit(5);
        }
    int bb = 0;
    for (int aa = 0; aa < 3; aa++)
        {
        if((dir = readdir(dd)) == NULL)
            break;
        bb++;
        }
    closedir(dd);

    if(bb > 2)
        {
        //printf("%s\n", dir->d_name);
        fprintf(stderr,"Mount Point: '%s\n", mountpoint);
        fprintf(stderr,"Mount failed. Reason: directory not empty or mounted already.\n");
        exit(5);
        }

    if(loglevel > 0)
        {
        syslog(LOG_DEBUG, "Started with %s\n", mountpoint);
        syslog(LOG_DEBUG, "Using data %s\n", mountsecret);
        }

    // Note: if you transform the file with a different block size
    // it will not decrypt.
    //bufsize = ss.st_blksize;
    //printf("Bufsize = %d\n", bufsize);

    bluepoint2_encrypt(decoy, sizeof(decoy), passx, plen);

    if (ondemand)
        {
        if(!passprog[0])
            {
            if(loglevel > 0)
                syslog(LOG_DEBUG, "Started with ondemand and no askpass proram");
            fprintf(stderr,"Must specify askpass program with the ondemand option.\n");
            exit(1);
            }
        // no pass asked for now
        }
    else
        {
        if(passx[0] == 0)
            {
            char tmp[MAXPASSLEN];

            if(passprog[0] != 0)
                {
                if(verbose)
                    printf("Getting pass from program: '%s'\n", passprog);

                char *res = hs_askpass(passprog, tmp, MAXPASSLEN);
                int rlen = strlen(res);
                unsigned long olen = 0;

                if (res && rlen)
                    {
                    unsigned char *res2 = base64_decode(res, rlen, &olen);
                    //strncpy(passx, res, sizeof(passx));
                    strncpy(passx, res2, sizeof(passx));
                    }
                else
                    {
                    if(loglevel > 0)
                        syslog(LOG_DEBUG, "Cannot obtain pass from: '%s'\n", passprog);

                    fprintf(stderr,"Cannot obtain pass from input.\n");
                    exit(4);
                    }
                }
            }
        // Will ask for pass if not filled
        int ret2 = pass_ritual(mountpoint, mountsecret, passx, &plen);
        if(ret2)
            {
            // Catch abort message
            if(ret2 == 2)
                fprintf(stderr, "Aborted.\n");
            else
                fprintf(stderr,"Invalid pass.\n");

            syslog(LOG_AUTH, "Authentication error on mounting by %d '%s' -> '%s'",
                                getuid(), mountsecret, mountpoint);
            exit(6);
            }
        }

    if(!quiet)
        {
        if(ondemand)
            printf("Mounting: '%s' with on-demand password.\n", mountpoint);
        else
            printf("Mounting: '%s'\n", mountpoint);
        }

    // Disable / Uncomment this when done
    syslog(LOG_DEBUG, "hsencfs pass from: len=%d '%s'\n", plen,
                                bluepoint2_dumphex(passx, plen));

    // Write back expanded paths
    char *argv2[3];
    argv2[0]  = mountsecret;    argv2[1]  = mountpoint;
    argv2[2]  = NULL;

    if(verbose)
        printf("Mount parms '%s' '%s'\n", mountsecret,  mountpoint);

    // Create INODE directory
    //char tmp2[PATH_MAX];
    //strncpy(tmp2, mountsecret, sizeof(tmp2)); strcat(tmp2, ".inodedata");
    //if(stat(tmp2, &ss) < 0)
    //    {
    //    if (mkdir(tmp2, 0700) < 0)
    //        {
    //        fprintf(stderr,"Cannot create inode data dir: '%s'\n", mountsecret);
    //        exit(3);
    //        }
    //    }

    // Skip arguments that are parsed already
    // Synthesize new array
    int ret = fuse_main(2, argv2, &xmp_oper, NULL);

    // FUSE MAIN terminates ...

    // Inform user, make a log entry
    if(ret)
        {
        if(loglevel > 0)
            syslog(LOG_DEBUG, "Mount returned with '%d'", ret);

        printf("Mounted by uid %d -> %s\n", getuid(), mountpoint);
        printf("Mount returned with '%d' errno=%d\n", ret, errno);

        syslog(LOG_AUTH, "Cannot mount, attempt by user %d '%s' -> '%s'",
                                         getuid(), mountsecret, mountpoint);
        }
    else
        {
        if(loglevel > 0)
            syslog(LOG_DEBUG, "Mounted '%s' data -> (%s)", mountpoint, mountsecret);

        printf("Mounted by uid %d -> %s\n", getuid(), mountpoint);

        syslog(LOG_AUTH, "Mounted  '%s' by %d data -> (%s)",
                                 mountpoint, getuid(), mountsecret);
        }
    return ret;
}

// EOF

