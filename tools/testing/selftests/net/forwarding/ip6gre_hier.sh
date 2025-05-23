#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Test IP-in-IP GRE tunnels without key.
# This test uses hierarchical topology for IP tunneling tests. See
# ip6gre_lib.sh for more details.

ALL_TESTS="
	gre_hier
	gre_mtu_change
	gre_hier_remote_change
"

NUM_NETIFS=6
source lib.sh
source ip6gre_lib.sh

setup_prepare()
{
	h1=${NETIFS[p1]}
	ol1=${NETIFS[p2]}

	ul1=${NETIFS[p3]}
	ul2=${NETIFS[p4]}

	ol2=${NETIFS[p5]}
	h2=${NETIFS[p6]}

	forwarding_enable
	vrf_prepare
	h1_create
	h2_create
	sw1_hierarchical_create $ol1 $ul1
	sw2_hierarchical_create $ol2 $ul2
}

gre_hier()
{
	test_traffic_ip4ip6 "GRE hierarchical IPv4-in-IPv6"
	test_traffic_ip6ip6 "GRE hierarchical IPv6-in-IPv6"
}

gre_mtu_change()
{
	test_mtu_change gre
}

gre_hier_remote_change()
{
	hier_remote_change

	test_traffic_ip4ip6 "GRE hierarchical IPv4-in-IPv6 (new remote)"
	test_traffic_ip6ip6 "GRE hierarchical IPv6-in-IPv6 (new remote)"

	hier_remote_restore

	test_traffic_ip4ip6 "GRE hierarchical IPv4-in-IPv6 (old remote)"
	test_traffic_ip6ip6 "GRE hierarchical IPv6-in-IPv6 (old remote)"
}

cleanup()
{
	pre_cleanup

	sw2_hierarchical_destroy $ol2 $ul2
	sw1_hierarchical_destroy $ol1 $ul1
	h2_destroy
	h1_destroy
	vrf_cleanup
	forwarding_restore
}

trap cleanup EXIT

setup_prepare
setup_wait
tests_run

exit $EXIT_STATUS
