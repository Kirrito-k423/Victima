#include "cuckoo_tlb.h"
#include <cmath>
#include <iostream>
#include <utility>
#include "memory_manager.h"


namespace ParametricDramDirectoryMSI
{
    UInt32 CUCKOO_TLB::SIM_PAGE_SHIFT;
    IntPtr CUCKOO_TLB::SIM_PAGE_SIZE;
    IntPtr CUCKOO_TLB::SIM_PAGE_MASK;

    ParametricDramDirectoryMSI::MemoryManager *CUCKOO_TLB::m_manager = NULL;

    CUCKOO_TLB::CUCKOO_TLB(String name, String cfgname, core_id_t core_id, ShmemPerfModel* _m_shmem_perf_model, 
        // Cuckoo hash related
        int d, char* hash_func, int size, float rehash_threshold, 
        uint8_t scale, // Control the scale of each rehash 
        uint8_t swaps, // Number of elements to be rehashed
        uint8_t priority, // Bias a hashtable during insertion
        // Cuckoo hash related end
        int* page_size_list, int page_sizes, PageTableWalker* _ptw)
        : m_core_id(core_id)
        , m_shmem_perf_model(_m_shmem_perf_model)
        {
            std::cout << "Instantiating Cuckoo Software-managed TLB" << std::endl;
            create_elastic(d, size, &elasticCuckooHT_4KB, hash_func, rehash_threshold, scale, swaps, priority); //4KB cuckoo
		    create_elastic(d, size, &elasticCuckooHT_2MB, hash_func, rehash_threshold, scale, swaps, priority); 
 
            cuckoo_latency = SubsecondTime::Zero();
            m_hits = m_miss = m_access = m_eviction = 0;
            
            registerStatsMetric(name, core_id, "hits", &m_hits);
            registerStatsMetric(name, core_id, "miss", &m_miss);
            registerStatsMetric(name, core_id, "latency", &cuckoo_latency);
            registerStatsMetric(name, core_id, "access", &m_access);
            registerStatsMetric(name, core_id, "eviction", &m_eviction);
            is_cuckoo_potm = true;

            ptw = _ptw; // Initialize page table walker

        }

    void CUCKOO_TLB::allocate(IntPtr address, SubsecondTime now, int level, Core::lock_signal_t lock_signal) { // allocate POM TLB entry in cuckoo tables
        int page_size = ptw->init_walk_functional(address);
        IntPtr vpn = address >> page_size;
        m_eviction++;

        elem_t elem_4KB;
        elem_4KB.valid = 1;
        elem_4KB.value = address >> 15; // We can assume 8 PTE entries/hash set

        elem_t elem_2MB;
        elem_2MB.valid = 1;
        elem_2MB.value = address >> 24; // We can assume 8 PTE entries/hash set


        SubsecondTime maxLat = SubsecondTime::Zero();
        SubsecondTime final_latency = SubsecondTime::Zero();
        now = getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_USER_THREAD);

        if(page_size == 21) {
            insert_elastic(&elem_2MB,&elasticCuckooHT_2MB, 0, 0);
            evaluate_elasticity(&elasticCuckooHT_2MB, 0);
        }

        else {
            insert_elastic(&elem_4KB,&elasticCuckooHT_4KB, 0, 0);
            evaluate_elasticity(&elasticCuckooHT_4KB, 0);
        }

    }

    CUCKOO_TLB::where_t CUCKOO_TLB::lookup(IntPtr address, SubsecondTime now, bool allocate_on_miss, int level, bool model_count, Core::lock_signal_t lock_signal, CacheCntlr* l1dcache) {
        int page_size = ptw->init_walk_functional(address);
        IntPtr vpn = address >> page_size;
        if(model_count) m_access++;
        
        elem_t elem_4KB;
        elem_4KB.valid = 1;
        elem_4KB.value = address >> 15; // We can assume 8 PTE entries/hash set
        std::vector<elem_t> accessedAddresses_4KB = find_elastic_ptw(&elem_4KB, &elasticCuckooHT_4KB);

        elem_t elem_2MB;
        elem_2MB.valid = 1;
        elem_2MB.value = address >> 24; // We can assume 8 PTE entries/hash set
        std::vector<elem_t> accessedAddresses_2MB = find_elastic_ptw(&elem_2MB, &elasticCuckooHT_2MB);

		bool found = false;
		bool found4KB = false;
		bool found2MB = false;

		for(elem_t elem: accessedAddresses_4KB) {
			if(elem.valid)
			{
				found = true;
				found4KB = true;
				m_hits++;
				break;
			}
		}

		for(elem_t elem: accessedAddresses_2MB) {
			if(elem.valid)
			{
				found = true;
				found2MB = true;
				m_hits++;
				break;
			}
		}

		if(!found) {
			if(page_size == 21) {
                insert_elastic(&elem_2MB, &elasticCuckooHT_2MB, 0, 0);
                evaluate_elasticity(&elasticCuckooHT_2MB,0);
			}
            else {
                insert_elastic(&elem_4KB, &elasticCuckooHT_4KB, 0, 0);
                evaluate_elasticity(&elasticCuckooHT_4KB, 0);
            }
            m_miss++;
        }
        SubsecondTime maxLat = SubsecondTime::Zero();
        SubsecondTime final_latency = SubsecondTime::Zero();
        // now = getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_USER_THREAD);
        SubsecondTime t_start = getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_USER_THREAD);
        if(found4KB || !found) {
            for(elem_t addr: accessedAddresses_4KB) {
                
                IntPtr cache_address = addr.value & (~((64 - 1))); 		
                CacheBlockInfo::block_type_t block_type = CacheBlockInfo::block_type_t::TLB_ENTRY;

                l1dcache->processMemOpFromCore(
                    0,
                    lock_signal,
                    Core::mem_op_t::READ,
                    cache_address, 0,
                    NULL, 64,
                    true,
                    true, block_type, SubsecondTime::Zero());
            }
        }
        SubsecondTime t_end = getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_USER_THREAD);
            // getShmemPerfModel()->setElapsedTime(ShmemPerfModel::_USER_THREAD , now);
            
            // ComponentLatency hash_latency = ComponentLatency(Sim()->getCoreManager()->getCoreFromID(m_core_id)->getDvfsDomain(),0); // Hash-function latency

            // if(!found && ((t_end - t_start + hash_latency.getLatency()) > maxLat))
            //     maxLat = (t_end - t_start) + hash_latency.getLatency();
            
            // if(found && addr.valid){
            //     final_latency =  t_end - t_start + hash_latency.getLatency();
            // }

        if(found2MB || !found){
            for(elem_t addr: accessedAddresses_2MB) {
                SubsecondTime t_start = getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_USER_THREAD);
				IntPtr cache_address = addr.value & (~((64 - 1))); 		
                CacheBlockInfo::block_type_t block_type =  CacheBlockInfo::block_type_t::TLB_ENTRY;

                l1dcache->processMemOpFromCore(
                    0,
                    lock_signal,
                    Core::mem_op_t::READ,
                    cache_address, 0,
                    NULL, 64,
                    true,
                    true, block_type, SubsecondTime::Zero());
                    
				SubsecondTime t_end = getShmemPerfModel()->getElapsedTime(ShmemPerfModel::_USER_THREAD);
				// getShmemPerfModel()->setElapsedTime(ShmemPerfModel::_USER_THREAD , now);
                
                // ComponentLatency hash_latency = ComponentLatency(Sim()->getCoreManager()->getCoreFromID(m_core_id)->getDvfsDomain(),0); // Hash-function latency
                
                // if(!found && ((t_end - t_start + hash_latency.getLatency()) > maxLat))
                //     maxLat = (t_end - t_start) + hash_latency.getLatency();
                // if(found && addr.valid){
                //     final_latency =  t_end - t_start + hash_latency.getLatency();
                // }
            }
        }
	// 	if(found) {
	// 		latency = final_latency;
    //         return where_t::HIT;
	// 	}
	// 	else {
	// 		latency = maxLat;
    //         return where_t::MISS;
	// 	}               
    // }
    }
}