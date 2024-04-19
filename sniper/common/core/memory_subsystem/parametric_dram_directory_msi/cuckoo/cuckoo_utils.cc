#include "cuckoo_utils.h"

uint64_t map_address_to_channel(uint64_t address, int channel, int channel_offset, int num_channel)
{
    int bits_for_channel = ceil(log2(num_channel));
    uint64_t mask = ((1ULL << bits_for_channel) - 1) << channel_offset;  // Mask for the bits to change
    address &= ~mask;  // Clear only the specific channel bits
    address |= (uint64_t)channel << channel_offset;  // Set the new channel bits
    return address;
}

uint64_t map_address_to_bank(uint64_t address, int bank, int dram_page_size, int num_bank, int channel_offset, int num_channel)
{
    int bits_for_channel = ceil(log2(num_channel));
    uint64_t mask_channel = ((1ULL << bits_for_channel) - 1) << channel_offset;  // Mask for the bits to change
    address &= ~mask_channel;  // Clear only the specific channel bits
    address |= (uint64_t)0 << channel_offset;  // map the address to channel 0

    int bank_offset = 6 + ceil(log2(dram_page_size));
    int bits_for_bank = ceil(log2(num_bank));
    uint64_t mask_bank = ((1ULL << bits_for_bank) - 1) << bank_offset;  // Mask for the bits to change
    address &= ~mask_bank;  // Clear only the specific channel bits
    address |= (uint64_t)bank << bank_offset;  // Set the new channel bits
    return address;
}