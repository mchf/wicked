/*
 * netinfo autoip supplicant
 *
 * Copyright (C) 2010 Olaf Kirch <okir@suse.de>
 */

#ifndef __WICKED_AUTOIP_PRIVATE_H__
#define __WICKED_AUTOIP_PRIVATE_H__

#include <wicked/socket.h>
#include "netinfo_priv.h"

typedef enum ni_autoip_state {
	NI_AUTOIP_STATE_INIT,
	NI_AUTOIP_STATE_CLAIMING,
	NI_AUTOIP_STATE_CLAIMED,
} ni_autoip_state_t;

typedef struct ni_autoip_device	ni_autoip_device_t;

struct ni_autoip_device {
	ni_autoip_device_t *	next;
	unsigned int		users;

	char *			ifname;
	ni_linkinfo_t		link;

	ni_arp_socket_t *	arp_socket;
	ni_capture_devinfo_t	devinfo;

	unsigned int		notify : 1,
				failed : 1;

	struct {
	    ni_autoip_state_t	state;
	    const ni_timer_t *	timer;
	} fsm;

	struct {
	    struct in_addr	candidate;
	    unsigned int	nprobes;
	    unsigned int	nclaims;
	    unsigned int	nconflicts;
	    time_t		last_defense;
	} autoip;

	ni_addrconf_lease_t *	lease;
};

extern ni_autoip_device_t *ni_autoip_active;

extern int		ni_autoip_acquire(ni_autoip_device_t *, ni_addrconf_request_t *);
extern int		ni_autoip_release(ni_autoip_device_t *, const ni_uuid_t *);

extern long             ni_autoip_fsm_get_timeout(void);
extern void             ni_autoip_fsm_check_timeout(void);
extern int		ni_autoip_fsm_select(ni_autoip_device_t *);
extern const char *     ni_autoip_fsm_state_name(ni_autoip_state_t);
extern int              ni_autoip_fsm_commit_lease(ni_autoip_device_t *, ni_addrconf_lease_t *);

extern ni_autoip_device_t *ni_autoip_device_new(const char *, const ni_linkinfo_t *);
extern ni_autoip_device_t *ni_autoip_device_get(ni_autoip_device_t *);
extern void		ni_autoip_device_put(ni_autoip_device_t *);
extern int              ni_autoip_device_start(ni_autoip_device_t *);
extern void             ni_autoip_device_stop(ni_autoip_device_t *);
extern void             ni_autoip_device_set_lease(ni_autoip_device_t *, ni_addrconf_lease_t *);
extern void             ni_autoip_device_drop_lease(ni_autoip_device_t *);
extern unsigned int     ni_autoip_device_uptime(const ni_autoip_device_t *, unsigned int);
extern ni_autoip_device_t *ni_autoip_device_by_index(unsigned int);
extern ni_autoip_device_t *ni_autoip_device_find(const char *);
extern int              ni_autoip_device_reconfigure(ni_autoip_device_t *, const ni_interface_t *);

#endif /* __WICKED_AUTOIP_PRIVATE_H__ */