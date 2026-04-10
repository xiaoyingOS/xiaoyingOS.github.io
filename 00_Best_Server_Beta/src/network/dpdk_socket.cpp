// DPDK Socket implementation

#include "best_server/network/dpdk_socket.hpp"
#include <cstring>

#if BEST_SERVER_PLATFORM_LINUX && defined(HAVE_DPDK)

namespace best_server {
namespace network {

bool DPKDSocket::dpdk_initialized_ = false;
struct rte_mempool* DPKDSocket::mbuf_pool_ = nullptr;

// DPKDSocket implementation
DPKDSocket::DPKDSocket() 
    : port_id_(RTE_MAX_ETHPORTS)
    , queue_id_(0)
    , bound_(false)
{
}

DPKDSocket::~DPKDSocket() {
    if (bound_) {
        stop();
    }
}

bool DPKDSocket::initialize(const DPDKConfig& config) {
    if (dpdk_initialized_) {
        return true;
    }
    
    // Convert string args to char* array
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>("best_server"));
    
    for (const auto& arg : config.eal_args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    
    int argc = static_cast<int>(argv.size());
    
    // Initialize EAL
    if (rte_eal_init(argc, argv.data()) < 0) {
        return false;
    }
    
    // Create mbuf pool
    char pool_name[64];
    snprintf(pool_name, sizeof(pool_name), "mbuf_pool_%d", rte_socket_id());
    
    mbuf_pool_ = rte_pktmbuf_pool_create(
        pool_name,
        config.mbuf_pool_size,
        config.mbuf_cache_size,
        0,
        RTE_MBUF_DEFAULT_BUF_SIZE,
        rte_socket_id()
    );
    
    if (!mbuf_pool_) {
        rte_eal_cleanup();
        return false;
    }
    
    dpdk_initialized_ = true;
    return true;
}

bool DPKDSocket::bind(const std::string& pci_addr, uint16_t port_id) {
    if (!dpdk_initialized_) {
        return false;
    }
    
    port_id_ = port_id;
    
    // Configure port
    struct rte_eth_conf port_conf;
    memset(&port_conf, 0, sizeof(port_conf));
    
    // Enable RSS, checksum offload, TSO
    port_conf.rxmode.mq_mode = ETH_MQ_RX_RSS;
    port_conf.txmode.mq_mode = ETH_MQ_TX_NONE;
    port_conf.rx_adv_conf.rss_conf.rss_hf = ETH_RSS_IP | ETH_RSS_TCP | ETH_RSS_UDP;
    
    // Configure port
    if (rte_eth_dev_configure(port_id_, 1, 1, &port_conf) != 0) {
        return false;
    }
    
    // Setup RX/TX queues
    if (rte_eth_rx_queue_setup(port_id_, 0, 512, rte_socket_id(), nullptr) != 0) {
        return false;
    }
    
    if (rte_eth_tx_queue_setup(port_id_, 0, 512, rte_socket_id(), nullptr) != 0) {
        return false;
    }
    
    bound_ = true;
    return true;
}

bool DPKDSocket::configure_queue(uint16_t queue_id, uint16_t nb_rx_desc, uint16_t nb_tx_desc) {
    if (!bound_) {
        return false;
    }
    
    queue_id_ = queue_id;
    
    // Setup RX queue
    if (rte_eth_rx_queue_setup(port_id_, queue_id, nb_rx_desc, rte_socket_id(), nullptr) != 0) {
        return false;
    }
    
    // Setup TX queue
    if (rte_eth_tx_queue_setup(port_id_, queue_id, nb_tx_desc, rte_socket_id(), nullptr) != 0) {
        return false;
    }
    
    return true;
}

bool DPKDSocket::start() {
    if (!bound_) {
        return false;
    }
    
    // Start port
    if (rte_eth_dev_start(port_id_) != 0) {
        return false;
    }
    
    // Set promiscuous mode
    rte_eth_promiscuous_enable(port_id_);
    
    // Wait for link up
    struct rte_eth_link link;
    int retries = 10;
    while (retries-- > 0) {
        rte_eth_link_get(port_id_, &link);
        if (link.link_status == ETH_LINK_UP) {
            break;
        }
        rte_delay_ms(100);
    }
    
    return true;
}

void DPKDSocket::stop() {
    if (bound_) {
        rte_eth_dev_stop(port_id_);
        rte_eth_promiscuous_disable(port_id_);
        bound_ = false;
    }
}

size_t DPKDSocket::receive(Packet* packets, size_t max_packets) {
    if (!bound_ || !packets || max_packets == 0) {
        return 0;
    }
    
    struct rte_mbuf* mbufs[MAX_BURST_SIZE];
    uint16_t burst_size = std::min(max_packets, static_cast<size_t>(MAX_BURST_SIZE));
    
    uint16_t nb_rx = rte_eth_rx_burst(port_id_, queue_id_, mbufs, burst_size);
    
    for (uint16_t i = 0; i < nb_rx; ++i) {
        packets[i].mbuf = mbufs[i];
        packets[i].data = rte_pktmbuf_mtod(mbufs[i], void*);
        packets[i].size = mbufs[i]->data_len;
        packets[i].timestamp_ns = rte_get_timer_cycles();
    }
    
    return nb_rx;
}

size_t DPKDSocket::send(const Packet* packets, size_t count) {
    if (!bound_ || !packets || count == 0) {
        return 0;
    }
    
    struct rte_mbuf* mbufs[MAX_BURST_SIZE];
    uint16_t burst_size = std::min(count, static_cast<size_t>(MAX_BURST_SIZE));
    
    // Prepare mbufs
    for (uint16_t i = 0; i < burst_size; ++i) {
        mbufs[i] = packets[i].mbuf;
    }
    
    uint16_t nb_tx = rte_eth_tx_burst(port_id_, queue_id_, mbufs, burst_size);
    
    // Free transmitted mbufs
    for (uint16_t i = nb_tx; i < burst_size; ++i) {
        rte_pktmbuf_free(mbufs[i]);
    }
    
    return nb_tx;
}

DPKDSocket::Statistics DPKDSocket::get_statistics() const {
    Statistics stats{};
    
    if (bound_) {
        struct rte_eth_stats eth_stats;
        if (rte_eth_stats_get(port_id_, &eth_stats) == 0) {
            stats.rx_packets = eth_stats.ipackets;
            stats.tx_packets = eth_stats.opackets;
            stats.rx_bytes = eth_stats.ibytes;
            stats.tx_bytes = eth_stats.obytes;
            stats.rx_errors = eth_stats.ierrors;
            stats.tx_errors = eth_stats.oerrors;
        }
    }
    
    return stats;
}

bool DPKDSocket::is_initialized() {
    return dpdk_initialized_;
}

// DPKDSocketFactory implementation
std::unique_ptr<DPKDSocket> DPKDSocketFactory::create_socket(
    const std::string& pci_addr,
    uint16_t port_id,
    uint16_t queue_id
) {
    auto socket = std::make_unique<DPKDSocket>();
    
    if (!socket->bind(pci_addr, port_id)) {
        return nullptr;
    }
    
    if (!socket->configure_queue(queue_id, 512, 512)) {
        return nullptr;
    }
    
    if (!socket->start()) {
        return nullptr;
    }
    
    return socket;
}

bool DPKDSocketFactory::initialize_dpdk(const DPDKConfig& config) {
    return DPKDSocket::initialize(config);
}

void DPKDSocketFactory::cleanup_dpdk() {
#if BEST_SERVER_PLATFORM_LINUX && defined(HAVE_DPDK)
    if (DPKDSocket::mbuf_pool_) {
        rte_mempool_free(DPKDSocket::mbuf_pool_);
        DPKDSocket::mbuf_pool_ = nullptr;
    }
    
    if (DPKDSocket::dpdk_initialized_) {
        rte_eal_cleanup();
        DPKDSocket::dpdk_initialized_ = false;
    }
#endif
}

} // namespace network
} // namespace best_server

#else // No DPDK support

namespace best_server {
namespace network {

bool DPKDSocket::dpdk_initialized_ = false;
struct rte_mempool* DPKDSocket::mbuf_pool_ = nullptr;

DPKDSocket::DPKDSocket() : port_id_(0), queue_id_(0), bound_(false) {}
DPKDSocket::~DPKDSocket() {}

bool DPKDSocket::initialize(const DPDKConfig&) { return false; }
bool DPKDSocket::bind(const std::string&, uint16_t) { return false; }
bool DPKDSocket::configure_queue(uint16_t, uint16_t, uint16_t) { return false; }
bool DPKDSocket::start() { return false; }
void DPKDSocket::stop() {}
size_t DPKDSocket::receive(Packet*, size_t) { return 0; }
size_t DPKDSocket::send(const Packet*, size_t) { return 0; }
DPKDSocket::Statistics DPKDSocket::get_statistics() const { return {}; }
bool DPKDSocket::is_initialized() { return false; }

std::unique_ptr<DPKDSocket> DPKDSocketFactory::create_socket(
    const std::string&, uint16_t, uint16_t) { return nullptr; }
bool DPKDSocketFactory::initialize_dpdk(const DPDKConfig&) { return false; }
void DPKDSocketFactory::cleanup_dpdk() {}

} // namespace network
} // namespace best_server

#endif // HAVE_DPDK