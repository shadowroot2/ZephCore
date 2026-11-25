#pragma once

//#include <Arduino.h>
#include <mesh/Mesh.h>
#include <string.h>

struct ChannelDetails {
  mesh::GroupChannel channel;
  char name[32];
};

/* The well-known open-channel key ("izOH6cXN6mrJ5e26oRXNcg=="): a channel
 * carrying it is a public channel, anything else is a private group. */
static const uint8_t CHANNEL_PUBLIC_PSK[16] = {
	0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
	0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72
};

static inline bool channel_is_public(const ChannelDetails &ch)
{
	return memcmp(ch.channel.secret, CHANNEL_PUBLIC_PSK,
		      sizeof(CHANNEL_PUBLIC_PSK)) == 0;
}
