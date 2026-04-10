// Best_Server Framework Unit Tests
// Simple unit tests for major components

#include <best_server/best_server.hpp>
#include <cassert>
#include <iostream>


// Test configuration manager
void test_config_manager() {
    std::cout << "Testing Config Manager..." << std::endl;
    
    auto config = best_server::config::Manager::create();
    config->set_string("test.key", "test_value");
    config->set_int("test.number", 42);
    config->set_bool("test.enabled", true);
    
    assert(config->get_string("test.key") == "test_value");
    assert(config->get_int("test.number") == 42);
    assert(config->get_bool("test.enabled") == true);
    
    assert(config->has("test.key"));
    assert(!config->has("nonexistent"));
    
    config->remove("test.key");
    assert(!config->has("test.key"));
    
    std::cout << "Config Manager tests passed!" << std::endl;
}

// Test metrics (disabled - monitoring module not enabled)
/*
void test_metrics() {
    std::cout << "Testing Metrics..." << std::endl;

    auto metrics = best_server::monitoring::MetricsRegistry::create();

    auto counter = metrics->counter("test_counter");
    counter->increment();
    counter->increment(5);

    auto value = counter->get();
    assert(std::get<int64_t>(value) == 6);

    auto gauge = metrics->gauge("test_gauge");
    gauge->set(100);
    gauge->increment(10);
    gauge->decrement(5);

    auto gauge_value = gauge->get();
    assert(std::get<double>(gauge_value) == 105.0);

    auto histogram = metrics->histogram("test_histogram", {1, 5, 10});
    histogram->observe(2);
    histogram->observe(7);
    histogram->observe(15);

    assert(histogram->count() == 3);

    std::cout << "Metrics tests passed!" << std::endl;
}
*/

// Test load balancer
void test_load_balancer() {
    std::cout << "Testing Load Balancer..." << std::endl;
    
    auto lb = best_server::load_balancer::LoadBalancer::create();
    
    best_server::load_balancer::Backend b1;
    b1.id = "backend-1";
    b1.address = "192.168.1.10";
    b1.port = 3000;
    b1.weight = 100;
    
    best_server::load_balancer::Backend b2;
    b2.id = "backend-2";
    b2.address = "192.168.1.11";
    b2.port = 3000;
    b2.weight = 200;
    
    lb->add_backend(b1);
    lb->add_backend(b2);

    [[maybe_unused]] auto stats = lb->statistics();
    assert(stats.healthy_backends == 2);

    [[maybe_unused]] auto backend = lb->next_backend();
    assert(backend != nullptr);

    std::cout << "Load Balancer tests passed!" << std::endl;
}

// Test service discovery
void test_service_discovery() {
    std::cout << "Testing Service Discovery..." << std::endl;
    
    auto registry = best_server::service_discovery::ServiceRegistry::create();
    
    best_server::service_discovery::ServiceInstance instance;
    instance.id = "test-service-1";
    instance.name = "test-service";
    instance.address = "127.0.0.1";
    instance.port = 8080;
    instance.healthy = true;
    
    registry->register_service(instance);
    
    auto services = registry->discover("test-service");
    assert(services.size() == 1);
    assert(services[0].id == "test-service-1");
    
    registry->deregister_service("test-service-1");
    services = registry->discover("test-service");
    assert(services.empty());
    
    std::cout << "Service Discovery tests passed!" << std::endl;
}

// Test HTTP request/response
void test_http() {
    std::cout << "Testing HTTP..." << std::endl;
    
    best_server::network::HTTPRequest request;
    request.set_method(best_server::network::HTTPMethod::GET);
    request.set_url("/api/users");
    request.set_header("Content-Type", "application/json");
    
    assert(request.method() == best_server::network::HTTPMethod::GET);
    assert(request.url() == "/api/users");
    assert(request.get_header("Content-Type") == "application/json");
    
    best_server::network::HTTPResponse response;
    response.set_status(best_server::network::HTTPStatus::OK);
    response.set_body("Hello, World!");
    
    assert(response.status() == best_server::network::HTTPStatus::OK);
    assert(response.body_string() == "Hello, World!");
    
    std::cout << "HTTP tests passed!" << std::endl;
}

// Test RPC (disabled - RPC module not enabled)
/*
void test_rpc() {
    std::cout << "Testing RPC..." << std::endl;

    auto server = best_server::rpc::RPCServer::create();

    bool handler_called = false;
    server->register_handler("test_method", [&handler_called](
        const best_server::rpc::RPCRequest& req, best_server::rpc::RPCResponse& resp) {
        handler_called = true;
        resp.result = R"({"success":true})";
    });

    // Simulate a request
    best_server::rpc::RPCRequest request;
    request.id = 1;
    request.method = "test_method";
    request.params = R"({"arg":"value"})";

    best_server::rpc::RPCResponse response;
    server->handle_request(request, response);

    assert(handler_called);
    assert(response.id == 1);
    assert(response.result == R"({"success":true})");

    std::cout << "RPC tests passed!" << std::endl;
}
*/

// Test logger (disabled - logger module not enabled)
/*
void test_logger() {
    std::cout << "Testing Logger..." << std::endl;

    auto logger = best_server::logger::Logger::create("test_logger");
    logger->set_level(best_server::logger::Level::Info);

    assert(logger->name() == "test_logger");
    assert(logger->level() == best_server::logger::Level::Info);

    logger->info("Test message");
    logger->warning("Warning message");
    logger->error("Error message");

    std::cout << "Logger tests passed!" << std::endl;
}
*/

// Main test runner
int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Best_Server Framework Unit Tests" << std::endl;
    std::cout << "========================================" << std::endl;

    try {
        test_config_manager();
        // test_metrics(); // disabled - monitoring module not enabled
        test_load_balancer();
        test_service_discovery();
        test_http();
        // test_rpc(); // disabled - RPC module not enabled
        // test_logger(); // disabled - logger module not enabled

        std::cout << "\n========================================" << std::endl;
        std::cout << "All enabled tests passed successfully!" << std::endl;
        std::cout << "========================================" << std::endl;

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}