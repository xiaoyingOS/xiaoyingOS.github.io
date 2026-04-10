// DPDK Socket - High-performance zero-copy networking with DPDK
//
// Implements DPDK-based networking for kernel bypass and zero-copy I/O:
// - DPDK PMD driver support
// - Zero-copy packet processing
// - Hardware offload (checksum, TSO)
// - Multi-queue support
// - NUMA-aware memory management

#ifndef BEST_SERVER_NETWORK_DPDK_SOCKET_HPP
#define BEST_SERVER_NETWORK_DPDK_SOCKET_HPP

#include <best_server/memory/zero_copy_buffer.hpp>
#include <string>
#include <vector>
#include <memory>
#include <atomic>

#if BEST_SERVER_PLATFORM_LINUX && defined(HAVE_DPDK)
    #include <rte_eal.h>
    #include <rte_ethdev.h>
    #include <rte_mbuf.h>
    #include <rte_mempool.h>
    #include <rte_ring.h>
#endif

namespace best_server {
namespace network {

// DPDK initialization configuration
struct DPDKConfig {
    std::vector<std::string> eal_args;  // EAL initialization arguments
    size_t mbuf_pool_size{8192};        // Number of mbufs in pool
    size_t mbuf_cache_size{256};         // Per-lcore cache size
    size_t rx_queue_size{512};           // RX queue size
    size_t tx_queue_size{512};           // TX queue size
    bool enable_checksum_offload{true};  // Enable hardware checksum
    bool enable_tso{true};               // Enable TCP segmentation offload
};

// Network packet (zero-copy)
struct Packet {
    void* data;
    size_t size;
    uint64_t timestamp_ns;
    
#if BEST_SERVER_PLATFORM_LINUX && defined(HAVE_DPDK)
    struct rte_mbuf* mbuf;  // DPDK mbuf (for zero-copy)
#endif
};

// DPDK socket for zero-copy networking
class DPKDSocket {
public:
    static constexpr size_t MAX_BURST_SIZE = 32;
    
    DPKDSocket();
    ~DPKDSocket();
    
    // Initialize DPDK EAL
    static bool initialize(const DPDKConfig& config);
    
    // Bind to PCI device
    bool bind(const std::string& pci_addr, uint16_t port_id);
    
    // Configure queue
    bool configure_queue(uint16_t queue_id, uint16_t nb_rx_desc, uint16_t nb_tx_desc);
    
    // Start port
    bool start();
    
    // Stop port
    void stop();
    
    // Receive packets (zero-copy)
    size_t receive(Packet* packets, size_t max_packets);
    
    // Send packets (zero-copy)
    size_t send(const Packet* packets, size_t count);
    
    // Get statistics
    struct Statistics {
        uint64_t rx_packets;
        uint64_t tx_packets;
        uint64_t rx_bytes;
        uint64_t tx_bytes;
        uint64_t rx_errors;
        uint64_t tx_errors;
    };
    Statistics get_statistics() const;
    
    // Check if initialized
    static bool is_initialized();
    
    // Get port ID
    uint16_t port_id() const { return port_id_; }
    
    // Get queue ID
    uint16_t queue_id() const { return queue_id_; }
    
private:
    uint16_t port_id_;
    uint16_t queue_id_;
    bool bound_;
    
#if BEST_SERVER_PLATFORM_LINUX && defined(HAVE_DPDK)
    static bool dpdk_initialized_;
    static struct rte_mempool* mbuf_pool_;
#endif
};

// DPDK socket factory
class DPKDSocketFactory {
public:
    static std::unique_ptr<DPKDSocket> create_socket(
        const std::string& pci_addr, 
        uint16_t port_id, 
        uint16_t queue_id
    );
    
    // Initialize DPDK singleton
    static bool initialize_dpdk(const DPDKConfig& config);
    
    // Cleanup DPDK
    static void cleanup_dpdk();
};

} // namespace network
} // namespace best_server

#endif // BEST_SERVER_NETWORK_DPDK_SOCKET_HPP