/*
 *  $Id: nfsroot.c,v 1.43 1997/10/16 19:55:27 mj Exp $
 *
 *  Copyright (C) 1995, 1996  Gero Kuhlmann <gero@gkminix.han.de>
 *
 *  Allow an NFS filesystem to be mounted as root. The way this works is:
 *     (1) Use the IP autoconfig mechanism to set local IP addresses and routes.
 *     (2) Handle RPC negotiation with the system which replied to RARP or
 *         was reported as a boot server by BOOTP or manually.
 *     (3) The actual mounting is done later, when init() is running.
 *
 *
 *	Changes:
 *
 *	Alan Cox	:	Removed get_address name clash with FPU.
 *	Alan Cox	:	Reformatted a bit.
 *	Gero Kuhlmann	:	Code cleanup
 *	Michael Rausch  :	Fixed recognition of an incoming RARP answer.
 *	Martin Mares	: (2.0)	Auto-configuration via BOOTP supported.
 *	Martin Mares	:	Manual selection of interface & BOOTP/RARP.
 *	Martin Mares	:	Using network routes instead of host routes,
 *				allowing the default configuration to be used
 *				for normal operation of the host.
 *	Martin Mares	:	Randomized timer with exponential backoff
 *				installed to minimize network congestion.
 *	Martin Mares	:	Code cleanup.
 *	Martin Mares	: (2.1)	BOOTP and RARP made configuration options.
 *	Martin Mares	:	Server hostname generation fixed.
 *	Gerd Knorr	:	Fixed wired inode handling
 *	Martin Mares	: (2.2)	"0.0.0.0" addresses from command line ignored.
 *	Martin Mares	:	RARP replies not tested for server address.
 *	Gero Kuhlmann	: (2.3) Some bug fixes and code cleanup again (please
 *				send me your new patches _before_ bothering
 *				Linus so that I don' always have to cleanup
 *				_afterwards_ - thanks)
 *	Gero Kuhlmann	:	Last changes of Martin Mares undone.
 *	Gero Kuhlmann	: 	RARP replies are tested for specified server
 *				again. However, it's now possible to have
 *				different RARP and NFS servers.
 *	Gero Kuhlmann	:	"0.0.0.0" addresses from command line are
 *				now mapped to INADDR_NONE.
 *	Gero Kuhlmann	:	Fixed a bug which prevented BOOTP path name
 *				from being used (thanks to Leo Spiekman)
 *	Andy Walker	:	Allow to specify the NFS server in nfs_root
 *				without giving a path name
 *	Swen Th�mmler	:	Allow to specify the NFS options in nfs_root
 *				without giving a path name. Fix BOOTP request
 *				for domainname (domainname is NIS domain, not
 *				DNS domain!). Skip dummy devices for BOOTP.
 *	Jacek Zapala	:	Fixed a bug which prevented server-ip address
 *				from nfsroot parameter from being used.
 *	Olaf Kirch	:	Adapted to new NFS code.
 *	Jakub Jelinek	:	Free used code segment.
 *	Marko Kohtala	:	Fixed some bugs.
 *	Martin Mares	:	Debug message cleanup
 *	Martin Mares	:	Changed to use the new generic IP layer autoconfig
 *				code. BOOTP and RARP moved there.
 *	Martin Mares	:	Default path now contains host name instead of
 *				host IP address (but host name defaults to IP
 *				address anyway).
 *	Martin Mares	:	Use root_server_addr appropriately during setup.
 */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/major.h>
#include <linux/utsname.h>
#include <net/ipconfig.h>

/* Define this to allow debugging output */
#undef NFSROOT_DEBUG
#define NFSDBG_FACILITY NFSDBG_ROOT

/* Default path we try to mount. "%s" gets replaced by our IP address */
#define NFS_ROOT		"/tftpboot/%s"
#define NFS_ROOT_NAME_LEN	256

/* Parameters passed from the kernel command line */
static char nfs_root_name[NFS_ROOT_NAME_LEN] __initdata = "default";
static int nfs_params_parsed = 0;

/* Address of NFS server */
static __u32 servaddr __initdata = 0;

/* Name of directory to mount */
static char nfs_path[NFS_MAXPATHLEN] __initdata = { 0, };

/* NFS-related data */
static struct nfs_mount_data nfs_data __initdata = { 0, };/* NFS mount info */
static int nfs_port __initdata = 0;		/* Port to connect to for NFS */
static int mount_port __initdata = 0;		/* Mount daemon port number */


/***************************************************************************

			     Parsing of options

 ***************************************************************************/

/*
 *  The following integer options are recognized
 */
static struct nfs_int_opts {
	char *name;
	int  *val;
} root_int_opts[] __initdata = {
	{ "port",	&nfs_port },
	{ "rsize",	&nfs_data.rsize },
	{ "wsize",	&nfs_data.wsize },
	{ "timeo",	&nfs_data.timeo },
	{ "retrans",	&nfs_data.retrans },
	{ "acregmin",	&nfs_data.acregmin },
	{ "acregmax",	&nfs_data.acregmax },
	{ "acdirmin",	&nfs_data.acdirmin },
	{ "acdirmax",	&nfs_data.acdirmax },
	{ NULL,		NULL }
};


/*
 *  And now the flag options
 */
static struct nfs_bool_opts {
	char *name;
	int  and_mask;
	int  or_mask;
} root_bool_opts[] __initdata = {
	{ "soft",	~NFS_MOUNT_SOFT,	NFS_MOUNT_SOFT },
	{ "hard",	~NFS_MOUNT_SOFT,	0 },
	{ "intr",	~NFS_MOUNT_INTR,	NFS_MOUNT_INTR },
	{ "nointr",	~NFS_MOUNT_INTR,	0 },
	{ "posix",	~NFS_MOUNT_POSIX,	NFS_MOUNT_POSIX },
	{ "noposix",	~NFS_MOUNT_POSIX,	0 },
	{ "cto",	~NFS_MOUNT_NOCTO,	0 },
	{ "nocto",	~NFS_MOUNT_NOCTO,	NFS_MOUNT_NOCTO },
	{ "ac",		~NFS_MOUNT_NOAC,	0 },
	{ "noac",	~NFS_MOUNT_NOAC,	NFS_MOUNT_NOAC },
	{ NULL,		0,			0 }
};


/*
 *  Prepare the NFS data structure and parse any options. This tries to
 *  set as many values in the nfs_data structure as known right now.
 */
__initfunc(static int root_nfs_name(char *name))
{
	char buf[NFS_MAXPATHLEN];
	char *cp, *cq, *options, *val;
	int octets = 0;

	if (nfs_params_parsed)
		return nfs_params_parsed;

	/* It is possible to override the server IP number here */
	cp = cq = name;
	while (octets < 4) {
		while (*cp >= '0' && *cp <= '9')
			cp++;
		if (cp == cq || cp - cq > 3)
			break;
		if (*cp == '.' || octets == 3)
			octets++;
		if (octets < 4)
			cp++;
		cq = cp;
	}
	if (octets == 4 && (*cp == ':' || *cp == '\0')) {
		if (*cp == ':')
			*cp++ = '\0';
		root_server_addr = in_aton(name);
		name = cp;
	}

	/* Clear the nfs_data structure and setup the server hostname */
	memset(&nfs_data, 0, sizeof(nfs_data));

	/* Set the name of the directory to mount */
	if (root_server_path[0] && !strcmp(name, "default"))
		strncpy(buf, root_server_path, NFS_MAXPATHLEN-1);
	else
		strncpy(buf, name, NFS_MAXPATHLEN-1);
	buf[NFS_MAXPATHLEN-1] = '\0';
	if ((options = strchr(buf, ',')))
		*options++ = '\0';
	if (!strcmp(buf, "default"))
		strcpy(buf, NFS_ROOT);
	cp = system_utsname.nodename;
	if (strlen(buf) + strlen(cp) > NFS_MAXPATHLEN) {
		printk(KERN_ERR "Root-NFS: Pathname for remote directory too long.\n");
		return -1;
	}
	sprintf(nfs_path, buf, cp);

	/* Set some default values */
	nfs_port          = -1;
	nfs_data.version  = NFS_MOUNT_VERSION;
	nfs_data.flags    = NFS_MOUNT_NONLM;	/* No lockd in nfs root yet */
	nfs_data.rsize    = NFS_DEF_FILE_IO_BUFFER_SIZE;
	nfs_data.wsize    = NFS_DEF_FILE_IO_BUFFER_SIZE;
	nfs_data.bsize	  = 0;
	nfs_data.timeo    = 7;
	nfs_data.retrans  = 3;
	nfs_data.acregmin = 3;
	nfs_data.acregmax = 60;
	nfs_data.acdirmin = 30;
	nfs_data.acdirmax = 60;

	/* Process any options */
	if (options) {
		cp = strtok(options, ",");
		while (cp) {
			if ((val = strchr(cp, '='))) {
				struct nfs_int_opts *opts = root_int_opts;
				*val++ = '\0';
				while (opts->name && strcmp(opts->name, cp))
					opts++;
				if (opts->name)
					*(opts->val) = (int) simple_strtoul(val, NULL, 10);
			} else {
				struct nfs_bool_opts *opts = root_bool_opts;
				while (opts->name && strcmp(opts->name, cp))
					opts++;
				if (opts->name) {
					nfs_data.flags &= opts->and_mask;
					nfs_data.flags |= opts->or_mask;
				}
			}
			cp = strtok(NULL, ",");
		}
	}
	return 1;
}


/*
 *  Get NFS server address.
 */
__initfunc(static int root_nfs_addr(void))
{
	if ((servaddr = root_server_addr) == INADDR_NONE) {
		printk(KERN_ERR "Root-NFS: No NFS server available, giving up.\n");
		return -1;
	}

	strncpy(nfs_data.hostname, in_ntoa(servaddr), sizeof(nfs_data.hostname)-1);
	nfs_data.namlen = strlen(nfs_data.hostname);
	return 0;
}

/*
 *  Tell the user what's going on.
 */
#ifdef NFSROOT_DEBUG
__initfunc(static void root_nfs_print(void))
{
	printk(KERN_NOTICE "Root-NFS: Mounting %s on server %s as root\n",
		nfs_path, nfs_data.hostname);
	printk(KERN_NOTICE "Root-NFS:     rsize = %d, wsize = %d, timeo = %d, retrans = %d\n",
		nfs_data.rsize, nfs_data.wsize, nfs_data.timeo, nfs_data.retrans);
	printk(KERN_NOTICE "Root-NFS:     acreg (min,max) = (%d,%d), acdir (min,max) = (%d,%d)\n",
		nfs_data.acregmin, nfs_data.acregmax,
		nfs_data.acdirmin, nfs_data.acdirmax);
	printk(KERN_NOTICE "Root-NFS:     nfsd port = %d, mountd port = %d, flags = %08x\n",
		nfs_port, mount_port, nfs_data.flags);
}
#endif


__initfunc(int root_nfs_init(void))
{
#ifdef NFSROOT_DEBUG
	nfs_debug |= NFSDBG_ROOT;
#endif

	/*
	 * Decode the root directory path name and NFS options from
	 * the kernel command line. This has to go here in order to
	 * be able to use the client IP address for the remote root
	 * directory (necessary for pure RARP booting).
	 */
	if (root_nfs_name(nfs_root_name) < 0 ||
	    root_nfs_addr() < 0)
		return -1;

#ifdef NFSROOT_DEBUG
	root_nfs_print();
#endif

	return 0;
}


/*
 *  Parse NFS server and directory information passed on the kernel
 *  command line.
 */
__initfunc(void nfs_root_setup(char *line, int *ints))
{
	ROOT_DEV = MKDEV(UNNAMED_MAJOR, 255);
	if (line[0] == '/' || line[0] == ',' || (line[0] >= '0' && line[0] <= '9')) {
		strncpy(nfs_root_name, line, sizeof(nfs_root_name));
		nfs_root_name[sizeof(nfs_root_name)-1] = '\0';
	} else {
		int n = strlen(line) + strlen(NFS_ROOT);
		if (n >= sizeof(nfs_root_name))
			line[sizeof(nfs_root_name) - strlen(NFS_ROOT) - 1] = '\0';
		sprintf(nfs_root_name, NFS_ROOT, line);
	}
	nfs_params_parsed = root_nfs_name(nfs_root_name);
}


/***************************************************************************

	       Routines to actually mount the root directory

 ***************************************************************************/

/*
 *  Construct sockaddr_in from address and port number.
 */
static inline void
set_sockaddr(struct sockaddr_in *sin, __u32 addr, __u16 port)
{
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = addr;
	sin->sin_port = port;
}

/*
 *  Query server portmapper for the port of a daemon program.
 */
__initfunc(static int root_nfs_getport(int program, int version))
{
	struct sockaddr_in sin;

	printk(KERN_NOTICE "Looking up port of RPC %d/%d on %s\n",
		program, version, in_ntoa(servaddr));
	set_sockaddr(&sin, servaddr, 0);
	return rpc_getport_external(&sin, program, version, IPPROTO_UDP);
}


/*
 *  Use portmapper to find mountd and nfsd port numbers if not overriden
 *  by the user. Use defaults if portmapper is not available.
 *  XXX: Is there any nfs server with no portmapper?
 */
__initfunc(static int root_nfs_ports(void))
{
	int port;

	if (nfs_port < 0) {
		if ((port = root_nfs_getport(NFS_PROGRAM, NFS_VERSION)) < 0) {
			printk(KERN_ERR "Root-NFS: Unable to get nfsd port "
					"number from server, using default\n");
			port = NFS_PORT;
		}
		nfs_port = htons(port);
		dprintk("Root-NFS: Portmapper on server returned %d "
			"as nfsd port\n", port);
	}

	if ((port = root_nfs_getport(NFS_MNT_PROGRAM, NFS_MNT_VERSION)) < 0) {
		printk(KERN_ERR "Root-NFS: Unable to get mountd port "
				"number from server, using default\n");
		port = NFS_MNT_PORT;
	}
	mount_port = htons(port);
	dprintk("Root-NFS: mountd port is %d\n", port);

	return 0;
}


/*
 *  Get a file handle from the server for the directory which is to be
 *  mounted.
 */
__initfunc(static int root_nfs_get_handle(void))
{
	struct sockaddr_in sin;
	int status;

	set_sockaddr(&sin, servaddr, mount_port);
	status = nfs_mount(&sin, nfs_path, &nfs_data.root);
	if (status < 0)
		printk(KERN_ERR "Root-NFS: Server returned error %d "
				"while mounting %s\n", status, nfs_path);

	return status;
}


/*
 *  Now actually mount the given directory.
 */
__initfunc(static int root_nfs_do_mount(struct super_block *sb))
{
	/* Pass the server address to NFS */
	set_sockaddr((struct sockaddr_in *) &nfs_data.addr, servaddr, nfs_port);

	/* Now (finally ;-)) read the super block for mounting */
	if (nfs_read_super(sb, &nfs_data, 1) == NULL)
		return -1;
	return 0;
}


/*
 *  Get the NFS port numbers and file handle, and then read the super-
 *  block for mounting.
 */
__initfunc(int nfs_root_mount(struct super_block *sb))
{
	if (root_nfs_init() < 0
	 || root_nfs_ports() < 0
	 || root_nfs_get_handle() < 0
	 || root_nfs_do_mount(sb) < 0)
		return -1;
	return 0;
}
