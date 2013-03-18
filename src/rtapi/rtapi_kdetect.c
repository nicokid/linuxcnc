#include "rtapi.h"
#include "rtapi_kdetect.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <dlfcn.h>
#include <sys/utsname.h>
#include <time.h>

typedef void (*pf)();
static const char *progname = "rtapi_kdetect";

int rtapi_kdetect(unsigned long *feat)
{
    struct stat sb;
    void *libxenomai, *libnative, *func;
    const char *errmsg;
    struct utsname u;
    FILE *fd;
    struct timespec ts;

    *feat = 0;
    if (uname(&u)) {
	rtapi_print_msg(RTAPI_MSG_ERR, "%s: uname(): %s\n",
			progname, strerror(errno));
	return -1;
    }

    // match u.release for soft hints
    if (strcasestr (u.release, "-rtai"))
	*feat |= UTSNAME_REL_RTAI;
    else if (strcasestr (u.release, "-rt"))
	*feat |= UTSNAME_REL_RT;
    if (strcasestr (u.release, "-xenomai"))
	*feat |= UTSNAME_REL_XENOMAI;

    // a hint of dubious quality
    if (strcasestr (u.version, "#rtai"))
	*feat |= UTSNAME_VER_RTAI;

    // check for ipipe patch - strong hint for RTAI or Xenomai
    if ((stat(PROC_IPIPE, &sb) == 0)  && ((sb.st_mode & S_IFMT) == S_IFDIR))
	*feat |= HAS_PROC_IPIPE;

    // a strong RT PREEMPT hint
    if (strcasestr (u.version, "PREEMPT RT"))
	*feat |= UTSNAME_VER_RT_PREEMPT;

    // a strong RT PREEMPT hint
    if ((fd = fopen(PREEMPT_RT_SYSFS,"r")) != NULL) {
	int flag;
	if ((fscanf(fd, "%d", &flag) == 1) && (flag))
	    *feat |= SYS_KERNEL_REALTIME_FOUND;
	fclose(fd);
    }

    // check for hires timers
    // this is really a sanitary requirement
    // it is 1ns on all kernels (RTAI, RT_PREEMPT, Xenomai, vanilla)
    // and so has no discriminatory value
    if (clock_getres(CLOCK_MONOTONIC, &ts)) {
	rtapi_print_msg(RTAPI_MSG_ERR, "%s: clock_getres(): %s\n",
			progname, strerror(errno));
	return -1;
    } else {
	if  ((ts.tv_sec == 0) && (ts.tv_nsec == 1))
	    *feat |= HAS_HIRES_TIMERS;
    }

    // check for Xenomai fingerprint(s) - strong hint
    if ((stat(XNHEAP_DEV_NAME, &sb) == 0)  && ((sb.st_mode & S_IFMT) == S_IFCHR))
	*feat |= XENO_RTHEAP_FOUND;

    // might be able to run without /proc/xenomai but it's reassuring if it exists
    if ((stat(PROC_XENOMAI, &sb) == 0) && ((sb.st_mode & S_IFMT) == S_IFDIR))
	*feat |= XENO_PROCENTRY_FOUND;

    // libnative on a non-xenomai kernel wont help, but checking for it might
    // yield a useful error message like 'xenomai libraries available but no xenomai
    // kernel running'
    if ((libxenomai = dlopen(LIBXENOMAI, RTLD_NOW)) != NULL)
	*feat |= XENO_LIBXENOMAI;

    if ((libnative = dlopen(LIBNATIVE, RTLD_NOW)) != NULL)
	*feat |= XENO_LIBNATIVE;

    // see if we can resolve an important symbol in libnative
    if ((libxenomai != NULL) && (libnative != NULL)) {
	func = (pf) dlsym(libnative, LIBNATIVE_SYM);
	if ( ((errmsg = dlerror()) == NULL) && (func != NULL))
	    *feat |= XENO_LIBSYMBOL;
    }
    if (libxenomai)
	dlclose(libxenomai);
    if (libnative)
	dlclose(libnative);

    // check for RTAI fingerprint(s) - this works only after 'realtime start'!
    if ((stat(DEV_RTAI_SHM, &sb) == 0)  && ((sb.st_mode & S_IFMT) == S_IFCHR))
	*feat |= DEV_RTAI_SHM_FOUND;

    return 0;
}


#ifdef TEST
int main(int argc, char **argv)
{
    unsigned long f;
    struct utsname u;

    rtapi_set_msg_level(RTAPI_MSG_DBG);
    if (uname(&u)) {
	rtapi_print_msg(RTAPI_MSG_ERR, "main: uname(): %s\n",
			strerror(errno));
	exit(1);
    }

    if (!rtapi_kdetect(&f)) {
	fprintf(stderr,"feature mask = 0x%lx\n",f);
	if (!(f & HAS_HIRES_TIMERS)) {
	    fprintf(stderr,
		    "cant detect hires timers. What clunker of a kernel is this? if postwar, please report a bug.\n");
	    exit(1);
	}
	if (f & XENO_RTHEAP_FOUND) {
	    fprintf(stderr, "a Xenomai kernel\n");
	    if ((f & (SYS_KERNEL_REALTIME_FOUND|UTSNAME_VER_RT_PREEMPT)) ==
		(SYS_KERNEL_REALTIME_FOUND|UTSNAME_VER_RT_PREEMPT)) {
		fprintf(stderr, ".. which also has RT PREEMPT patches applied\n");
	    }
	    if (!(f & UTSNAME_REL_XENOMAI))
		fprintf(stderr, "utsname.release looked less than helpful: '%s'\n",
			u.release);

	    if (f & (UTSNAME_REL_RTAI|UTSNAME_REL_RT))
		fprintf(stderr, "utsname.release looks contradictory: '%s'\n",
			u.release);

	    if ((f & (XENO_LIBXENOMAI|XENO_LIBNATIVE|XENO_LIBSYMBOL)) ==
		(XENO_LIBXENOMAI|XENO_LIBNATIVE|XENO_LIBSYMBOL))
		fprintf(stderr, "Xenomai userland library support looks intact\n");
	    else {
		if (!(f & XENO_LIBXENOMAI))
		    fprintf(stderr, "%s unusable\n", LIBXENOMAI);
		if (!(f & XENO_LIBNATIVE))
		    fprintf(stderr, "%s unusable\n", LIBNATIVE);
		if (!(f & XENO_LIBSYMBOL))
		    fprintf(stderr, "cant resolve %s in %s\n", LIBNATIVE_SYM, LIBNATIVE);
	    }
	} else {
	    if ((f & (SYS_KERNEL_REALTIME_FOUND|UTSNAME_VER_RT_PREEMPT)) ==
		(SYS_KERNEL_REALTIME_FOUND|UTSNAME_VER_RT_PREEMPT)) {
		fprintf(stderr, "an RT PREEMPT kernel\n");
	    } else {
		if (f & HAS_PROC_IPIPE) {
		    fprintf(stderr, "an RTAI kernel with RT modules %sloaded\n",
			    (f & DEV_RTAI_SHM_FOUND) ? "" : "not ");
		} else {
		    fprintf(stderr, "assuming a vanilla kernel\n");
		    if (f & UTSNAME_REL_RTAI)
			fprintf(stderr, "however, utsname.release hints at RTAI: '%s'\n",
				u.release);
		    if (f & UTSNAME_VER_RTAI)
			fprintf(stderr, "however, utsname.version hints at RTAI: '%s'\n",
				u.version);
		    if (f & UTSNAME_REL_XENOMAI)
			fprintf(stderr, "however, utsname.release hints at Xenomai: '%s'\n",
				u.release);
		    if (f & UTSNAME_REL_RT)
			fprintf(stderr, "however, utsname.release hints at RT_PREEMPT: '%s'\n",
				u.release);
		}
	    }
	}
    } else
	fprintf(stderr,"rtapi_kdetect failed\n");
    exit(0);
}
#endif
