#include <stdint.h>
#include <cmath> 
#include <iostream>
//This function map_address_to_channel map an address to a specific channel
uint64_t map_address_to_channel(uint64_t address, int channel, int channel_offset, int num_channel);

//This function map_address_to_bank map an address to a specific bank of channel 0
uint64_t map_address_to_bank(uint64_t address, int bank, int dram_page_size, int num_bank, int channel_offset, int num_channel);