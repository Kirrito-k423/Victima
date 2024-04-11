#include "cuckoo_utils.h"

uint64_t map_address_to_channel(uint64_t address, int channel, int channel_offset, int num_channel)
{
    int bits_for_channel = ceil(log2(num_channel));
    uint64_t mask = 0xFFFFFFFFFFFFFFFFULL - ((1ULL << bits_for_channel) - 1) << channel_offset;
    address &= mask;
    address |= (uint64_t)channel << channel_offset;
    return address;
}