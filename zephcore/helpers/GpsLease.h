/* SPDX-License-Identifier: MIT */
#pragma once

/*
 * GpsLease — who owns the companion's GPS receiver right now. Every feature
 * that needs a fix is a lease holder, the wp/geo save pipeline included (it
 * force-leases the GPS for one fix, ADR 0011). The answers here are the one
 * place that decides whether the GPS is free: a refusal names this owner, the
 * last holder out consults it, and a manual `gps off` is only legal when the
 * lease is free.
 */

enum gps_lease_owner {
	GPS_LEASE_FREE = 0,
	GPS_LEASE_TRACKING,
	GPS_LEASE_NAV,
	GPS_LEASE_SOS,
	GPS_LEASE_SAVE,
	GPS_LEASE_GEO,
};

/* The lease is held when any holder is active. Priority order (first holder
 * answers for the group): tracking, nav, sos, save, geo. */
static inline enum gps_lease_owner
gps_lease_held_by(bool tracking, bool nav, bool sos, bool save, bool geo)
{
	if (tracking) {
		return GPS_LEASE_TRACKING;
	}
	if (nav) {
		return GPS_LEASE_NAV;
	}
	if (sos) {
		return GPS_LEASE_SOS;
	}
	if (save) {
		return GPS_LEASE_SAVE;
	}
	if (geo) {
		return GPS_LEASE_GEO;
	}
	return GPS_LEASE_FREE;
}
