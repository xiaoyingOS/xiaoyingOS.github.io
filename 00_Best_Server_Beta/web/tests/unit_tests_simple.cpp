// Best_Server Framework Simple Unit Tests
// Test each component individually to identify hanging

#include <best_server/best_server.hpp>
#include <cassert>
#include <iostream>
#include <unistd.h>

// Test configuration manager
void test_config_manager() {
    std::cout << "  Starting Config Manager test..." << std::endl;
    
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
    
    std::cout << "  Config Manager tests passed!" << std::endl;
}

// Test HTTP request/response
void test_http() {
    std::cout << "  Starting HTTP test..." << std::endl;
    
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
    
    std::cout << "  HTTP tests passed!" << std::endl;
}

// Test load balancer
void test_load_balancer() {
    std::cout << "  Starting Load Balancer test..." << std::endl;
    
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

    std::cout << "  Load Balancer tests passed!" << std::endl;
}

// Test service discovery
void test_service_discovery() {
    std::cout << "  Starting Service Discovery test..." << std::endl;
    
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
    
    std::cout << "  Service Discovery tests passed!" << std::endl;
}

// Main test runner
int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Best_Server Framework Unit Tests" << std::endl;
    std::cout << "========================================" << std::endl;

    try {
        // Test 1: Config Manager
        std::cout << "\n[1/4] Testing Config Manager" << std::endl;
        test_config_manager();
        
        // Test 2: HTTP
        std::cout << "\n[2/4] Testing HTTP" << std::endl;
        test_http();
        
        // Test 3: Load Balancer
        std::cout << "\n[3/4] Testing Load Balancer" << std::endl;
        test_load_balancer();
        
        // Test 4: Service Discovery
        std::cout << "\n[4/4] Testing Service Discovery" << std::endl;
        test_service_discovery();

        std::cout << "\n========================================" << std::endl;
        std::cout << "All tests passed successfully!" << std::endl;
        std::cout << "========================================" << std::endl;

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
