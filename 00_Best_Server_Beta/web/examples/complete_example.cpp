// Complete Best_Server Framework Example
// This example demonstrates all major features of the Best_Server framework

#include <best_server/best_server.hpp>
#include <iostream>
#include <thread>
#include <chrono>

using namespace best_server;

int main() {
    // Initialize monitoring
    monitoring::Monitoring::initialize("best_server_example");
    
    // Get logger
    auto logger = logger::Logger::get("example");
    logger->set_level(logger::Level::Info);
    
    logger->info("Best_Server Framework Example");
    logger->info("==============================");
    
    // Example 1: Configuration Management
    logger->info("\n--- Configuration Management ---");
    auto config = config::Manager::create();
    config->set_string("server.address", "0.0.0.0");
    config->set_int("server.port", 8080);
    config->set_bool("server.ssl_enabled", true);
    
    logger->info("Server address: {}", config->get_string("server.address"));
    logger->info("Server port: {}", config->get_int("server.port"));
    logger->info("SSL enabled: {}", config->get_bool("server.ssl_enabled"));
    
    // Example 2: Metrics Collection
    logger->info("\n--- Metrics Collection ---");
    auto metrics = monitoring::Monitoring::metrics();
    auto http_requests = metrics->counter("http_requests_total", "Total HTTP requests");
    auto response_time = metrics->histogram("http_response_time_ms", {1, 5, 10, 50, 100, 500, 1000});
    
    // Simulate requests
    for (int i = 0; i < 100; i++) {
        http_requests->increment();
        response_time->observe(10 + (rand() % 100));
    }
    
    logger->info("Total requests: {}", std::get<int64_t>(http_requests->get()));
    logger->info("Prometheus export:\n{}", metrics->export_prometheus());
    
    // Example 3: Performance Monitoring
    logger->info("\n--- Performance Monitoring ---");
    auto perf = monitoring::Monitoring::performance();
    
    auto start = std::chrono::high_resolution_clock::now();
    // Simulate some work
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    perf->record_operation("database_query", duration);
    
    logger->info("Average query time: {}us", perf->get_average_duration("database_query").count());
    
    // Example 4: Distributed Tracing
    logger->info("\n--- Distributed Tracing ---");
    auto tracer = monitoring::Monitoring::tracer();
    
    auto span = tracer->start_span("handle_request");
    span->set_tag("http.method", "GET");
    span->set_tag("http.path", "/api/users");
    
    auto db_span = tracer->start_span("database_query", span);
    db_span->set_tag("db.query", "SELECT * FROM users");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    db_span->finish();
    
    auto cache_span = tracer->start_span("cache_lookup", span);
    cache_span->set_tag("cache.key", "user:123");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    cache_span->finish();
    
    span->finish();
    
    logger->info("Request processing time: {}us", span->duration().count());
    
    // Example 5: Service Discovery
    logger->info("\n--- Service Discovery ---");
    auto registry = service_discovery::ServiceRegistry::create();
    
    service_discovery::ServiceInstance instance;
    instance.id = "web-server-1";
    instance.name = "web-server";
    instance.address = "192.168.1.10";
    instance.port = 8080;
    instance.healthy = true;
    
    registry->register_service(instance);
    
    auto services = registry->discover("web-server");
    logger->info("Discovered {} service instances", services.size());
    
    // Example 6: Health Checking
    logger->info("\n--- Health Checking ---");
    auto health_checker = health::HealthChecker::create();
    
    health::CheckConfig http_check_config;
    http_check_config.check_name = "web_server_health";
    http_check_config.endpoint = "http://localhost:8080/health";
    http_check_config.expected_status = 200;
    http_check_config.interval = 5000;  // 5 seconds
    
    auto http_check = health::HTTPHealthCheck::create(http_check_config);
    http_check->set_status_callback([](health::Status status) {
        logger->info("Health check status: {}",
            health::utils::status_to_string(status));
    });
    
    health_checker->add_check(http_check);
    health_checker->start();
    
    // Example 7: Load Balancing
    logger->info("\n--- Load Balancing ---");
    auto load_balancer = load_balancer::LoadBalancer::create(
        load_balancer::Strategy::LeastConnections);
    
    load_balancer::Backend backend1;
    backend1.id = "backend-1";
    backend1.address = "192.168.1.20";
    backend1.port = 3000;
    backend1.weight = 100;
    
    load_balancer::Backend backend2;
    backend2.id = "backend-2";
    backend2.address = "192.168.1.21";
    backend2.port = 3000;
    backend2.weight = 100;
    
    load_balancer->add_backend(backend1);
    load_balancer->add_backend(backend2);
    
    auto selected = load_balancer->next_backend();
    if (selected) {
        logger->info("Selected backend: {}:{}", selected->address, selected->port);
    }
    
    // Example 8: RPC
    logger->info("\n--- RPC Framework ---");
    auto rpc_server = rpc::RPCServer::create();
    
    rpc_server->register_handler("add", [](const rpc::RPCRequest& req, rpc::RPCResponse& resp) {
        // Parse params and compute sum
        logger->info("RPC call: add({})", req.params);
        resp.result = R"({"result":42})";
    });
    
    rpc_server->register_handler("multiply", [](const rpc::RPCRequest& req, rpc::RPCResponse& resp) {
        logger->info("RPC call: multiply({})", req.params);
        resp.result = R"({"result":84})";
    });
    
    logger->info("RPC server registered {} handlers", 2);
    
    // Example 9: Database (Mock)
    logger->info("\n--- Database ---");
    logger->info("Redis client configured");
    logger->info("MySQL client configured");
    logger->info("PostgreSQL client configured");
    
    // Example 10: Alerting
    logger->info("\n--- Alerting ---");
    auto alert_manager = monitoring::Monitoring::alerts();
    
    auto alert = std::make_shared<monitoring::Alert>("high_error_rate", "High error rate detected");
    alert->set_callback([](const monitoring::Alert& alert) {
        logger->error("Alert triggered: {}", alert.description());
    });
    
    alert_manager->register_alert(alert);
    
    logger->info("Alert system configured");
    
    // Cleanup
    health_checker->stop();
    
    logger->info("\n=== Example completed successfully ===");
    
    return 0;
}