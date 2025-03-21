#include "../include/logger.hpp"
#include "../include/thread_pool.hpp"
#include "../include/memory_pool.hpp"
#include "../include/object_pool.hpp"
#include "../include/timer.hpp"
#include "../include/ipc_implement.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <iomanip>

// Test the logger module
void test_logger() {
    std::cout << "\n===== Test the logger module =====\n" << std::endl;
    
    // Configure the logger
    util::log::LogConfig config;
    config.use_stdout = true;  // Output to the console for easy viewing
    config.min_level = util::log::Level::DEBUG;
    util::log::Logger::instance().configure(config);
    
    // Test different levels of logging
    LOG_DEBUG("This is a debug log");
    LOG_INFO("This is an info log");
    LOG_WARNING("This is a warning log");
    LOG_ERROR("This is an error log");
    
    std::cout << "Logger test completed\n" << std::endl;
}

// Test the thread pool
void test_thread_pool() {
    std::cout << "\n===== Test the thread pool =====\n" << std::endl;
    
    util::thread::ThreadPool pool(4);
    std::cout << "Created a thread pool with " << pool.get_thread_count() << " threads" << std::endl;
    
    // Submit some tasks
    std::vector<std::future<int>> results;
    
    for (int i = 0; i < 8; ++i) {
        auto result = pool.enqueue([i] {
            std::cout << "Task " << i << " is executed on thread " 
                      << std::this_thread::get_id() << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100 * i));
            return i * i;
        });
        results.push_back(std::move(result));
    }
    
    // Get the results
    for (size_t i = 0; i < results.size(); ++i) {
        std::cout << "Task " << i << " result: " << results[i].get() << std::endl;
    }
    
    std::cout << "Thread pool test completed\n" << std::endl;
}

// Test the memory pool
void test_memory_pool() {
    std::cout << "\n===== Test the memory pool =====\n" << std::endl;
    
    auto& pool = util::memory::MemoryPool::GetInstance();
    
    // Allocate and deallocate different sizes of memory
    std::vector<void*> pointers;
    std::vector<size_t> sizes = {8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192};
    
    // Allocate
    for (auto size : sizes) {
        void* ptr = pool.Allocate(size);
        pointers.push_back(ptr);
        std::cout << "Allocated " << size << " bytes of memory: " << ptr << std::endl;
    }
    
    // Print statistics
    std::cout << "\nMemory pool statistics:" << std::endl;
    pool.PrintStats();
    
    // Deallocate
    for (size_t i = 0; i < pointers.size(); ++i) {
        pool.Deallocate(pointers[i], sizes[i]);
        std::cout << "Deallocated " << sizes[i] << " bytes of memory: " << pointers[i] << std::endl;
    }
    
    // Test smart pointers
    std::cout << "\nTest memory pool smart pointers:" << std::endl;
    {
        auto ptr = util::memory::make_pool_ptr<std::string>("This is a string allocated from the memory pool");
        std::cout << "Created a memory pool smart pointer: " << *ptr << std::endl;
    }
    
    // Test the new array allocation feature
    std::cout << "\nTest memory pool array allocation:" << std::endl;
    {
        auto arr = util::memory::make_pool_array<int>(10);
        for (int i = 0; i < 10; ++i) {
            arr[i] = i * i;
        }
        std::cout << "Created a memory pool array, first 5 elements: ";
        for (int i = 0; i < 5; ++i) {
            std::cout << arr[i] << " ";
        }
        std::cout << std::endl;
    }
    
    // Test memory usage statistics
    std::cout << "\nTest memory usage statistics:" << std::endl;
    {
        // Allocate a batch of memory to increase usage
        std::vector<void*> test_ptrs;
        for (int i = 0; i < 100; ++i) {
            test_ptrs.push_back(pool.Allocate(64));
        }
        
        std::cout << "Current memory usage: " << (pool.GetMemoryUsage() * 100.0) << "%" << std::endl;
        std::cout << "Current large allocation count: " << pool.GetLargeAllocations() << std::endl;
        
        // Deallocate memory
        for (auto ptr : test_ptrs) {
            pool.Deallocate(ptr, 64);
        }
    }
    
    // Test the memory pool's Trim feature
    std::cout << "\nTest the memory pool's Trim feature:" << std::endl;
    pool.Trim();
    
    // Print statistics again
    std::cout << "\nMemory pool statistics after deallocation:" << std::endl;
    pool.PrintStats();
    
    std::cout << "Memory pool test completed\n" << std::endl;
}

// Test the object pool
void test_object_pool() {
    std::cout << "\n===== Test the object pool =====\n" << std::endl;
    
    // Create a simple object for testing
    class TestObject {
    public:
        TestObject() : value_(0) { 
            std::cout << "TestObject constructed" << std::endl; 
        }
        
        explicit TestObject(int val) : value_(val) { 
            std::cout << "TestObject constructed with value " << val << std::endl; 
        }
        
        ~TestObject() { 
            std::cout << "TestObject destroyed, value was " << value_ << std::endl; 
        }
        
        void setValue(int val) { value_ = val; }
        int getValue() const { return value_; }
        
        void reset() { 
            value_ = 0; 
            std::cout << "TestObject reset" << std::endl; 
        }
        
    private:
        int value_;
        // Add some padding to make the object larger
        char padding_[64];
    };
    
    // Test basic object pool functionality
    std::cout << "\n----- Basic object pool functionality -----\n" << std::endl;
    
    // Create an object pool with default settings
    util::pool::ObjectPool<TestObject> pool(5);
    
    std::cout << "Initial pool stats - Available: " << pool.available_count() 
              << ", Active: " << pool.active_count() 
              << ", Total: " << pool.total_count() << std::endl;
    
    // Acquire some objects
    std::vector<TestObject*> objects;
    for (int i = 0; i < 3; ++i) {
        TestObject* obj = pool.acquire();
        obj->setValue(i + 1);
        objects.push_back(obj);
        
        std::cout << "Acquired object " << i << " with value " << obj->getValue() << std::endl;
    }
    
    std::cout << "After acquisition - Available: " << pool.available_count() 
              << ", Active: " << pool.active_count() 
              << ", Total: " << pool.total_count() << std::endl;
    
    // Release some objects
    for (size_t i = 0; i < objects.size(); ++i) {
        std::cout << "Releasing object " << i << " with value " << objects[i]->getValue() << std::endl;
        pool.release(objects[i]);
    }
    objects.clear();
    
    std::cout << "After release - Available: " << pool.available_count() 
              << ", Active: " << pool.active_count() 
              << ", Total: " << pool.total_count() << std::endl;
    
    // Test with reset function
    std::cout << "\n----- Object pool with reset function -----\n" << std::endl;
    
    util::pool::ObjectPool<TestObject> reset_pool(
        5, 
        util::pool::GrowthPolicy::Multiplicative,
        2,
        [](TestObject& obj) { obj.reset(); }
    );
    
    // Acquire and release an object to see reset in action
    TestObject* obj = reset_pool.acquire();
    obj->setValue(42);
    std::cout << "Object value before release: " << obj->getValue() << std::endl;
    reset_pool.release(obj);
    
    // Acquire the same object again and check its value
    obj = reset_pool.acquire();
    std::cout << "Object value after reset and reacquisition: " << obj->getValue() << std::endl;
    reset_pool.release(obj);
    
    // Test growth policies
    std::cout << "\n----- Growth policy tests -----\n" << std::endl;
    
    // Test multiplicative growth
    util::pool::ObjectPool<TestObject> mult_pool(
        2,  // Start with just 2 objects
        util::pool::GrowthPolicy::Multiplicative,
        2   // Double when empty
    );
    
    std::cout << "Multiplicative pool initial size: " << mult_pool.total_count() << std::endl;
    
    // Acquire more objects than initial size
    std::vector<TestObject*> mult_objects;
    for (int i = 0; i < 5; ++i) {
        mult_objects.push_back(mult_pool.acquire());
    }
    
    std::cout << "Multiplicative pool after growth - Total: " << mult_pool.total_count() << std::endl;
    
    // Release all objects
    for (auto obj : mult_objects) {
        mult_pool.release(obj);
    }
    mult_objects.clear();
    
    // Test additive growth
    util::pool::ObjectPool<TestObject> add_pool(
        2,  // Start with just 2 objects
        util::pool::GrowthPolicy::Additive,
        3   // Add 3 when empty
    );
    
    std::cout << "Additive pool initial size: " << add_pool.total_count() << std::endl;
    
    // Acquire more objects than initial size
    std::vector<TestObject*> add_objects;
    for (int i = 0; i < 5; ++i) {
        add_objects.push_back(add_pool.acquire());
    }
    
    std::cout << "Additive pool after growth - Total: " << add_pool.total_count() << std::endl;
    
    // Release all objects
    for (auto obj : add_objects) {
        add_pool.release(obj);
    }
    add_objects.clear();
    
    // Test fixed policy (no growth)
    util::pool::ObjectPool<TestObject> fixed_pool(
        3,  // Start with 3 objects
        util::pool::GrowthPolicy::Fixed,
        0   // Growth size doesn't matter for fixed policy
    );
    
    std::cout << "Fixed pool initial size: " << fixed_pool.total_count() << std::endl;
    
    // Try to acquire more objects than available
    std::vector<TestObject*> fixed_objects;
    for (int i = 0; i < 5; ++i) {
        TestObject* obj = fixed_pool.acquire();
        if (obj) {
            fixed_objects.push_back(obj);
            std::cout << "Acquired object " << i << std::endl;
        } else {
            std::cout << "Failed to acquire object " << i << " (pool empty)" << std::endl;
        }
    }
    
    std::cout << "Fixed pool after attempted growth - Total: " << fixed_pool.total_count() << std::endl;
    
    // Release all objects
    for (auto obj : fixed_objects) {
        fixed_pool.release(obj);
    }
    fixed_objects.clear();
    
    // Test RAII wrapper
    std::cout << "\n----- RAII wrapper tests -----\n" << std::endl;
    
    util::pool::ObjectPool<TestObject> raii_pool(5);
    
    {
        // Create a scope for the pooled object
        auto pooled_obj = util::pool::make_pooled(raii_pool);
        pooled_obj->setValue(100);
        
        std::cout << "Pooled object value: " << pooled_obj->getValue() << std::endl;
        std::cout << "During RAII scope - Available: " << raii_pool.available_count() 
                  << ", Active: " << raii_pool.active_count() << std::endl;
    } // pooled_obj goes out of scope here and is automatically returned to the pool
    
    std::cout << "After RAII scope - Available: " << raii_pool.available_count() 
              << ", Active: " << raii_pool.active_count() << std::endl;
    
    // Test multithreaded usage
    std::cout << "\n----- Multithreaded object pool tests -----\n" << std::endl;
    
    util::pool::ObjectPool<TestObject> mt_pool(
        20,  // Start with 20 objects
        util::pool::GrowthPolicy::Multiplicative,
        2,
        [](TestObject& obj) { obj.reset(); }
    );
    
    std::cout << "Initial pool state - Available: " << mt_pool.available_count() 
              << ", Active: " << mt_pool.active_count() << std::endl;
    
    const int num_threads = 4;
    const int ops_per_thread = 1000;
    
    auto thread_func = [&mt_pool, ops_per_thread](int thread_id) {
        for (int i = 0; i < ops_per_thread; ++i) {
            // Acquire an object
            auto obj = util::pool::make_pooled(mt_pool);
            
            // Do something with the object
            obj->setValue(thread_id * 1000 + i);
            
            // Object will be automatically returned to the pool
        }
    };
    
    // Start threads
    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(thread_func, i);
    }
    
    // Wait for all threads to complete
    for (auto& t : threads) {
        t.join();
    }
    
    std::cout << "Final pool state - Available: " << mt_pool.available_count() 
              << ", Active: " << mt_pool.active_count() 
              << ", Total: " << mt_pool.total_count() << std::endl;
    
    // Test trim functionality
    std::cout << "\n----- Trim functionality tests -----\n" << std::endl;
    
    // Create a pool and grow it
    util::pool::ObjectPool<TestObject> trim_pool(5);
    
    // Acquire and release many objects to grow the pool
    for (int i = 0; i < 20; ++i) {
        auto obj = trim_pool.acquire();
        trim_pool.release(obj);
    }
    
    std::cout << "Before trim - Available: " << trim_pool.available_count() << std::endl;
    
    // Trim the pool
    size_t removed = trim_pool.trim(10);
    
    std::cout << "Trimmed " << removed << " objects" << std::endl;
    std::cout << "After trim - Available: " << trim_pool.available_count() << std::endl;
    
    std::cout << "Object pool test completed\n" << std::endl;
}

// Test the timer
void test_timer() {
    std::cout << "\n===== Test the timer module =====\n" << std::endl;
    
    // Test a one-time timer
    auto timer_id = util::timer::schedule_once(500, []() {
        std::cout << "One-time timer triggered" << std::endl;
    });
    
    std::cout << "Created a one-time timer, ID: " << timer_id << std::endl;
    
    // Wait for the timer to execute
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    std::cout << "Timer test completed\n" << std::endl;
}

// Test IPC communication
void test_ipc_communication() {
    std::cout << "\n===== Test IPC communication (shared memory implementation) =====\n" << std::endl;
    
    const std::string ipc_name = "test_ipc";
    
    // Create server and client IPC implementations
    std::cout << "Creating server and client..." << std::endl;
    util::ipc::IPCImplement server_ipc(ipc_name, true);  // Server
    util::ipc::IPCImplement client_ipc(ipc_name, false); // Client
    
    // Start IPC communication
    std::cout << "Starting server..." << std::endl;
    server_ipc.start();
    
    std::cout << "Starting client..." << std::endl;
    client_ipc.start();
    
    // Wait for startup to complete
    std::cout << "Waiting for IPC channel to establish connection..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Test bidirectional communication
    std::cout << "\n===== Test bidirectional communication =====\n" << std::endl;
    
    // Client sends a request
    std::cout << "Client sending request..." << std::endl;
    client_ipc.sendMessage("Client request: Get current time");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    server_ipc.sendMessage("Server response: Current time is " + 
                              std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Client sends another request
    std::cout << "Client sending another request..." << std::endl;
    client_ipc.sendMessage("Client request: Get system information");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    server_ipc.sendMessage("Server response: System information - Linux x86_64");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Test concurrent bidirectional communication
    std::cout << "\n===== Test concurrent bidirectional communication =====\n" << std::endl;
    
    // Create two threads, one for client sending and one for server sending
    std::thread client_send_thread([&client_ipc]() {
        for (int i = 0; i < 10; i++) {
            std::string msg = "Client concurrent message #" + std::to_string(i);
            client_ipc.sendMessage(msg);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    
    std::thread server_send_thread([&server_ipc]() {
        for (int i = 0; i < 10; i++) {
            std::string msg = "Server concurrent message #" + std::to_string(i);
            server_ipc.sendMessage(msg);
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
    });

    
    // Wait for threads to complete
    client_send_thread.join();
    server_send_thread.join();
    
    // Wait for a while to ensure all messages are processed
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Test performance
    std::cout << "\n===== Test IPC performance =====\n" << std::endl;
    
    const int message_count = 1000;
    std::cout << "Sending " << message_count << " messages for performance testing..." << std::endl;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Client sends a large number of messages
    for (int i = 0; i < message_count; i++) {
        client_ipc.sendMessage("Performance test message #" + std::to_string(i));
    }

    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    // Stop IPC communication
    std::cout << "Stopping IPC communication..." << std::endl;
    server_ipc.stop();
    client_ipc.stop();
    
    std::cout << "IPC communication test completed\n" << std::endl;
}


// Performance test: Compare standard allocation and memory pool
void benchmark_memory_pool() {
    std::cout << "\n===== Memory pool performance test =====\n" << std::endl;
    
    // Reduce iteration count to avoid long running times
    const int iterations = 10000;
    const std::vector<size_t> alloc_sizes = {16, 64, 256, 1024, 4096};
    
    std::cout << "Testing performance of different allocation sizes..." << std::endl;
    
    for (size_t alloc_size : alloc_sizes) {
        std::cout << "\nTesting size: " << alloc_size << " bytes" << std::endl;
        
        // Use standard allocator
        auto start_std = std::chrono::high_resolution_clock::now();
        std::vector<void*> std_ptrs;
        std_ptrs.reserve(iterations);
        
        for (int i = 0; i < iterations; ++i) {
            std_ptrs.push_back(malloc(alloc_size));
        }
        
        for (auto ptr : std_ptrs) {
            free(ptr);
        }
        std_ptrs.clear();
        
        auto end_std = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> std_time = end_std - start_std;
        
        // Use memory pool
        auto& pool = util::memory::MemoryPool::GetInstance();
        auto start_pool = std::chrono::high_resolution_clock::now();
        std::vector<void*> pool_ptrs;
        pool_ptrs.reserve(iterations);
        
        for (int i = 0; i < iterations; ++i) {
            pool_ptrs.push_back(pool.Allocate(alloc_size));
        }
        
        for (auto ptr : pool_ptrs) {
            pool.Deallocate(ptr, alloc_size);
        }
        
        auto end_pool = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> pool_time = end_pool - start_pool;
        
        // Print results
        std::cout << "Standard allocator: " << std::fixed << std::setprecision(2) << std_time.count() << " ms" << std::endl;
        std::cout << "Memory pool: " << std::fixed << std::setprecision(2) << pool_time.count() << " ms" << std::endl;
        std::cout << "Performance improvement: " << std::fixed << std::setprecision(2) << (std_time.count() / pool_time.count()) << "x" << std::endl;
    }
    
    // Test performance in multithreaded scenario
    std::cout << "\nTesting memory pool performance in multithreaded scenario..." << std::endl;
    const int thread_count = 4;
    const int per_thread_iterations = iterations / thread_count;
    
    // Standard allocator multithreaded test
    auto start_std_mt = std::chrono::high_resolution_clock::now();
    
    std::vector<std::thread> std_threads;
    for (int t = 0; t < thread_count; ++t) {
        std_threads.emplace_back([per_thread_iterations]() {
            std::vector<void*> ptrs;
            ptrs.reserve(per_thread_iterations);
            
            for (int i = 0; i < per_thread_iterations; ++i) {
                ptrs.push_back(malloc(64));
            }
            
            for (auto ptr : ptrs) {
                free(ptr);
            }
        });
    }
    
    for (auto& t : std_threads) {
        t.join();
    }
    
    auto end_std_mt = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> std_mt_time = end_std_mt - start_std_mt;
    
    // Memory pool multithreaded test
    auto& pool = util::memory::MemoryPool::GetInstance();
    auto start_pool_mt = std::chrono::high_resolution_clock::now();
    
    std::vector<std::thread> pool_threads;
    for (int t = 0; t < thread_count; ++t) {
        pool_threads.emplace_back([per_thread_iterations, &pool]() {
            std::vector<void*> ptrs;
            ptrs.reserve(per_thread_iterations);
            
            for (int i = 0; i < per_thread_iterations; ++i) {
                ptrs.push_back(pool.Allocate(64));
            }
            
            for (auto ptr : ptrs) {
                pool.Deallocate(ptr, 64);
            }
        });
    }
    
    for (auto& t : pool_threads) {
        t.join();
    }
    
    auto end_pool_mt = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> pool_mt_time = end_pool_mt - start_pool_mt;
    
    // Print multithreaded test results
    std::cout << "Multithreaded standard allocator: " << std::fixed << std::setprecision(2) << std_mt_time.count() << " ms" << std::endl;
    std::cout << "Multithreaded memory pool: " << std::fixed << std::setprecision(2) << pool_mt_time.count() << " ms" << std::endl;
    std::cout << "Multithreaded performance improvement: " << std::fixed << std::setprecision(2) << (std_mt_time.count() / pool_mt_time.count()) << "x" << std::endl;
}

// Performance test: Compare standard allocation and object pool
void benchmark_object_pool() {
    std::cout << "\n===== Object pool performance test =====\n" << std::endl;
    
    // Test class
    class BenchmarkObject {
    public:
        BenchmarkObject() : data_(0) {}
        explicit BenchmarkObject(int val) : data_(val) {}
        
        void setData(int val) { data_ = val; }
        int getData() const { return data_; }
        
        void reset() { data_ = 0; }
        
    private:
        int data_;
        // Add some padding to make the object larger
        char buffer_[128];
    };
    
    const int iterations = 100000;
    
    std::cout << "Testing performance with " << iterations << " iterations..." << std::endl;
    
    // Test standard new/delete
    auto start_std = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        auto obj = new BenchmarkObject(i);
        obj->setData(i * 2);
        delete obj;
    }
    
    auto end_std = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> std_time = end_std - start_std;
    
    // Test object pool
    util::pool::ObjectPool<BenchmarkObject> pool(
        1000,  // Initial size
        util::pool::GrowthPolicy::Multiplicative,
        2,
        [](BenchmarkObject& obj) { obj.reset(); }
    );
    
    auto start_pool = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        auto obj = pool.acquire();
        obj->setData(i * 2);
        pool.release(obj);
    }
    
    auto end_pool = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> pool_time = end_pool - start_pool;
    
    // Test RAII wrapper
    auto start_raii = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        auto obj = util::pool::make_pooled(pool);
        obj->setData(i * 2);
    }
    
    auto end_raii = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> raii_time = end_raii - start_raii;
    
    // Print results
    std::cout << "Standard new/delete: " << std::fixed << std::setprecision(2) 
              << std_time.count() << " ms" << std::endl;
    std::cout << "Object pool: " << std::fixed << std::setprecision(2) 
              << pool_time.count() << " ms" << std::endl;
    std::cout << "Object pool with RAII: " << std::fixed << std::setprecision(2) 
              << raii_time.count() << " ms" << std::endl;
    
    std::cout << "Performance improvement (vs new/delete):" << std::endl;
    std::cout << "  - Basic object pool: " << std::fixed << std::setprecision(2) 
              << (std_time.count() / pool_time.count()) << "x" << std::endl;
    std::cout << "  - RAII object pool: " << std::fixed << std::setprecision(2) 
              << (std_time.count() / raii_time.count()) << "x" << std::endl;
    
    // Test multithreaded performance
    std::cout << "\nTesting multithreaded performance..." << std::endl;
    
    const int thread_count = 4;
    const int per_thread_iterations = iterations / thread_count;
    
    // Standard allocation multithreaded test
    auto start_std_mt = std::chrono::high_resolution_clock::now();
    
    std::vector<std::thread> std_threads;
    for (int t = 0; t < thread_count; ++t) {
        std_threads.emplace_back([per_thread_iterations]() {
            for (int i = 0; i < per_thread_iterations; ++i) {
                auto obj = new BenchmarkObject(i);
                obj->setData(i * 2);
                delete obj;
            }
        });
    }
    
    for (auto& t : std_threads) {
        t.join();
    }
    
    auto end_std_mt = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> std_mt_time = end_std_mt - start_std_mt;
    
    // Object pool multithreaded test
    util::pool::ObjectPool<BenchmarkObject> mt_pool(
        1000,  // Initial size
        util::pool::GrowthPolicy::Multiplicative,
        2,
        [](BenchmarkObject& obj) { obj.reset(); }
    );
    
    auto start_pool_mt = std::chrono::high_resolution_clock::now();
    
    std::vector<std::thread> pool_threads;
    for (int t = 0; t < thread_count; ++t) {
        pool_threads.emplace_back([per_thread_iterations, &mt_pool]() {
            for (int i = 0; i < per_thread_iterations; ++i) {
                auto obj = mt_pool.acquire();
                obj->setData(i * 2);
                mt_pool.release(obj);
            }
        });
    }
    
    for (auto& t : pool_threads) {
        t.join();
    }
    
    auto end_pool_mt = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> pool_mt_time = end_pool_mt - start_pool_mt;
    
    // Print multithreaded test results
    std::cout << "Multithreaded standard allocation: " << std::fixed << std::setprecision(2) 
              << std_mt_time.count() << " ms" << std::endl;
    std::cout << "Multithreaded object pool: " << std::fixed << std::setprecision(2) 
              << pool_mt_time.count() << " ms" << std::endl;
    std::cout << "Multithreaded performance improvement: " << std::fixed << std::setprecision(2) 
              << (std_mt_time.count() / pool_mt_time.count()) << "x" << std::endl;
    
    std::cout << "Object pool performance test completed\n" << std::endl;
}

int main() {
    // Test the logger module
    test_logger();
    
    // Test the thread pool
    test_thread_pool();
    
    // Test the memory pool
    test_memory_pool();
    
    // Test the object pool
    test_object_pool();
    
    // Test the timer
    test_timer();
    
    // Test IPC communication
    test_ipc_communication();
    
    // Performance tests
    benchmark_memory_pool();
    
    // Object pool performance test
    benchmark_object_pool();
    
    return 0;
}
