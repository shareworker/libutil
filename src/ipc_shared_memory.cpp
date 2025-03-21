#include "ipc_shared_memory.hpp"
#include "ipc_packet.hpp"
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

namespace util {
namespace ipc {

IPCSharedMemory::IPCSharedMemory(const std::string& ipc_name, bool is_server)
    : ipc_name_(ipc_name), is_server_(is_server), shm_key_(0), shm_id_(-1), shm_buffer_(nullptr),
      sem_key_(0), sem_id_(-1), initialized_(false) {}

IPCSharedMemory::~IPCSharedMemory() {
    Uninit();
}

bool IPCSharedMemory::Init() {
    if (ipc_name_.empty()) {
        LOG_ERROR("IPC name is empty");
        return false;
    }

    // Generate keys for shared memory and semaphores
    shm_key_ = GenerateKey(ipc_name_, false);
    sem_key_ = GenerateKey(ipc_name_, true);

    if (shm_key_ == -1 || sem_key_ == -1) {
        LOG_ERROR("Failed to generate keys for shared memory or semaphores");
        return false;
    }

    // Create shared memory
    if (!CreateSharedMemory()) {
        LOG_ERROR("Failed to create shared memory");
        return false;
    }

    // Create semaphores
    if (!CreateSemaphore()) {
        LOG_ERROR("Failed to create semaphores");
        DestroySharedMemory();
        return false;
    }

    // Initialize buffer if we're the server
    if (is_server_) {
        // Initialize header
        shm_buffer_->header.server_write_pos.store(0);
        shm_buffer_->header.server_read_pos.store(0);
        shm_buffer_->header.client_write_pos.store(0);
        shm_buffer_->header.client_read_pos.store(0);
    }

    initialized_ = true;
    LOG_DEBUG("Shared memory initialized successfully (is_server=%d)", is_server_);
    return true;
}

bool IPCSharedMemory::Uninit() {
    bool result = true;

    if (shm_buffer_) {
        if (shmdt(shm_buffer_) == -1) {
            LOG_ERROR("Failed to detach shared memory: %s", strerror(errno));
            result = false;
        }
        shm_buffer_ = nullptr;
    }

    // Only destroy resources if we're the server
    if (is_server_) {
        if (shm_id_ != -1) {
            if (!DestroySharedMemory()) {
                LOG_ERROR("Failed to destroy shared memory");
                result = false;
            }
        }

        if (sem_id_ != -1) {
            if (!DestroySemaphore()) {
                LOG_ERROR("Failed to destroy semaphores");
                result = false;
            }
        }
    }

    initialized_ = false;
    return result;
}

bool IPCSharedMemory::WritePacket(const IPCPacket& packet) {
    if (!initialized_ || !shm_buffer_) {
        LOG_ERROR("Shared memory not initialized");
        return false;
    }
    
    // Determine which buffer and positions to use based on server/client role
    char* buffer = nullptr;
    std::atomic<uint32_t>& write_pos = is_server_ ? 
        shm_buffer_->header.server_write_pos : 
        shm_buffer_->header.client_write_pos;
    std::atomic<uint32_t>& read_pos = is_server_ ? 
        shm_buffer_->header.client_read_pos : 
        shm_buffer_->header.server_read_pos;
    int write_sem = is_server_ ? SEM_SERVER_WRITE : SEM_CLIENT_WRITE;
    int read_sem = is_server_ ? SEM_CLIENT_READ : SEM_SERVER_READ;
    
    // Select the appropriate buffer
    buffer = is_server_ ? shm_buffer_->server_to_client : shm_buffer_->client_to_server;
    
    // Get packet size
    uint32_t packet_size = packet.GetTotalSize();
    
    // Check if packet fits in buffer
    if (packet_size > SHM_BUFFER_SIZE) {
        LOG_ERROR("Packet size %u exceeds buffer size %u", packet_size, SHM_BUFFER_SIZE);
        return false;
    }
    
    // Wait for write semaphore
    if (!SemaphoreWait(write_sem)) {
        LOG_ERROR("Failed to wait for write semaphore");
        return false;
    }
    
    // Get current write position
    uint32_t current_write_pos = write_pos.load(std::memory_order_acquire);
    uint32_t current_read_pos = read_pos.load(std::memory_order_acquire);
    
    // Check if there's enough space (considering circular buffer)
    uint32_t available_space;
    if (current_write_pos >= current_read_pos) {
        // Write position is after or equal to read position
        // Available space = total size - (write position - read position)
        available_space = SHM_BUFFER_SIZE - (current_write_pos - current_read_pos);
    } else {
        // Write position is before read position
        // Available space = read position - write position
        available_space = current_read_pos - current_write_pos;
    }
    
    // Ensure there's enough space to write the packet
    if (available_space <= packet_size) {
        LOG_ERROR("Not enough space in buffer: available=%u, needed=%u", available_space, packet_size);
        SemaphoreSignal(write_sem);
        return false;
    }
    
    // Serialize packet to buffer
    if (!packet.Serialize(buffer + current_write_pos, SHM_BUFFER_SIZE - current_write_pos)) {
        LOG_ERROR("Failed to serialize packet");
        SemaphoreSignal(write_sem);
        return false;
    }
    
    // Update write position
    uint32_t new_write_pos = (current_write_pos + packet_size) % SHM_BUFFER_SIZE;
    write_pos.store(new_write_pos, std::memory_order_release);
    
    // Signal read semaphore to indicate data is available
    SemaphoreSignal(read_sem);
    
    // Release write semaphore
    SemaphoreSignal(write_sem);
    
    return true;
}

bool IPCSharedMemory::ReadPacket(IPCPacket* packet) {
    if (!initialized_ || !shm_buffer_ || !packet) {
        LOG_ERROR("Shared memory not initialized or packet is null");
        return false;
    }
    
    // Determine which buffer and positions to use based on server/client role
    char* buffer = nullptr;
    std::atomic<uint32_t>& write_pos = is_server_ ? 
        shm_buffer_->header.client_write_pos : 
        shm_buffer_->header.server_write_pos;
    std::atomic<uint32_t>& read_pos = is_server_ ? 
        shm_buffer_->header.server_read_pos : 
        shm_buffer_->header.client_read_pos;
    int write_sem = is_server_ ? SEM_CLIENT_WRITE : SEM_SERVER_WRITE;
    int read_sem = is_server_ ? SEM_SERVER_READ : SEM_CLIENT_READ;
    
    // Select the appropriate buffer
    buffer = is_server_ ? shm_buffer_->client_to_server : shm_buffer_->server_to_client;
    
    // Check if there's data available (non-blocking check)
    uint32_t current_write_pos = write_pos.load(std::memory_order_acquire);
    uint32_t current_read_pos = read_pos.load(std::memory_order_acquire);
    if (current_read_pos == current_write_pos) {
        // No data available
        return false;
    }
    
    // Wait for read semaphore
    if (!SemaphoreWait(read_sem)) {
        LOG_ERROR("Failed to wait for read semaphore");
        return false;
    }
    
    // Re-check positions since they may have changed while waiting for semaphore
    current_read_pos = read_pos.load(std::memory_order_acquire);
    current_write_pos = write_pos.load(std::memory_order_acquire);
    
    // If no data is available, release semaphore and return
    if (current_read_pos == current_write_pos) {
        SemaphoreSignal(read_sem);
        return false;
    }
    
    // Read packet header to get total size
    util::ipc::PacketHeader header;
    std::memcpy(&header, buffer + current_read_pos, sizeof(header));
    
    // Validate magic ID
    if (header.magic_id != IPC_PACKET_MAGIC) {
        LOG_ERROR("Invalid packet magic ID: %u", header.magic_id);
        SemaphoreSignal(read_sem);
        return false;
    }
    
    // Calculate packet total size, including header, payload, and checksum
    uint32_t packet_size = sizeof(PacketHeader) + header.payload_len + sizeof(uint32_t);
    
    if (packet_size > SHM_BUFFER_SIZE) {
        LOG_ERROR("Packet size %u exceeds buffer size %u", packet_size, SHM_BUFFER_SIZE);
        SemaphoreSignal(read_sem);
        return false;
    }
    
    // Create new packet from buffer data
    *packet = IPCPacket(buffer + current_read_pos, packet_size);
    
    // Validate packet
    if (!packet->IsValid()) {
        LOG_ERROR("Invalid packet read from shared memory");
        SemaphoreSignal(read_sem);
        return false;
    }
    
    // Update read position
    uint32_t new_read_pos = (current_read_pos + packet_size) % SHM_BUFFER_SIZE;
    read_pos.store(new_read_pos, std::memory_order_release);
    
    // Signal write semaphore to indicate buffer space is available
    SemaphoreSignal(write_sem);
    
    return true;
}

key_t IPCSharedMemory::GenerateKey(const std::string& name, bool is_sem) {
    // Generate a unique key based on the IPC name and type
    // Use a hash of the name instead of ftok since ftok requires an existing file
    key_t key = 0;
    std::string key_str = name + (is_sem ? "_sem" : "_shm");
    
    // Simple hash function to generate a key from the string
    for (char c : key_str) {
        key = ((key << 5) + key) + c; // hash * 33 + c
    }
    
    // Ensure key is positive and not IPC_PRIVATE (0)
    key = (key & 0x7FFFFFFF);
    if (key == 0) {
        key = 1;
    }
    
    LOG_DEBUG("Generated key %d for %s", key, key_str.c_str());
    return key;
}

bool IPCSharedMemory::CreateSharedMemory() {
    // Try to get existing shared memory
    shm_id_ = shmget(shm_key_, sizeof(SharedMemoryBuffer), 0);
    
    if (shm_id_ == -1) {
        // Create new shared memory if it doesn't exist
        shm_id_ = shmget(shm_key_, sizeof(SharedMemoryBuffer), IPC_CREAT | SHM_PERMISSIONS);
        
        if (shm_id_ == -1) {
            LOG_ERROR("Failed to create shared memory: %s", strerror(errno));
            return false;
        }
        
        LOG_DEBUG("Created new shared memory segment with ID %d", shm_id_);
    } else {
        LOG_DEBUG("Using existing shared memory segment with ID %d", shm_id_);
    }
    
    // Attach to shared memory
    shm_buffer_ = (SharedMemoryBuffer*)shmat(shm_id_, nullptr, 0);
    
    if (shm_buffer_ == (void*)-1) {
        LOG_ERROR("Failed to attach to shared memory: %s", strerror(errno));
        shm_buffer_ = nullptr;
        return false;
    }
    
    return true;
}

bool IPCSharedMemory::DestroySharedMemory() {
    if (shm_id_ != -1) {
        if (shmctl(shm_id_, IPC_RMID, nullptr) == -1) {
            LOG_ERROR("Failed to destroy shared memory: %s", strerror(errno));
            return false;
        }
        shm_id_ = -1;
    }
    return true;
}

bool IPCSharedMemory::CreateSemaphore() {
    // Try to get existing semaphore
    sem_id_ = semget(sem_key_, SEM_COUNT, 0);
    
    if (sem_id_ == -1) {
        // Create new semaphore if it doesn't exist
        sem_id_ = semget(sem_key_, SEM_COUNT, IPC_CREAT | SHM_PERMISSIONS);
        
        if (sem_id_ == -1) {
            LOG_ERROR("Failed to create semaphore: %s", strerror(errno));
            return false;
        }
        
        LOG_DEBUG("Created new semaphore set with ID %d", sem_id_);
        
        // Initialize semaphores if we created them
        if (is_server_) {
            union semun {
                int val;
                struct semid_ds* buf;
                unsigned short* array;
            } arg;
            
            // Initialize all semaphores
            for (int i = 0; i < SEM_COUNT; i++) {
                // Initialize write semaphores to 1 (available)
                // and read semaphores to 0 (not available until data is written)
                int init_val = (i == SEM_SERVER_WRITE || i == SEM_CLIENT_WRITE) ? 1 : 0;
                
                arg.val = init_val;
                
                if (semctl(sem_id_, i, SETVAL, arg) == -1) {
                    LOG_ERROR("Failed to initialize semaphore %d: %s", i, strerror(errno));
                    return false;
                }
            }
            
            LOG_DEBUG("Initialized semaphores");
        }
    } else {
        LOG_DEBUG("Using existing semaphore set with ID %d", sem_id_);
    }
    
    return true;
}

bool IPCSharedMemory::DestroySemaphore() {
    if (sem_id_ != -1) {
        if (semctl(sem_id_, 0, IPC_RMID) == -1) {
            LOG_ERROR("Failed to destroy semaphore: %s", strerror(errno));
            return false;
        }
        sem_id_ = -1;
    }
    return true;
}

bool IPCSharedMemory::SemaphoreWait(int sem_index) {
    if (sem_id_ == -1) {
        LOG_ERROR("Invalid semaphore ID");
        return false;
    }
    
    struct sembuf op;
    op.sem_num = sem_index;
    op.sem_op = -1;  // Decrement by 1
    op.sem_flg = 0;  // Wait indefinitely
    
    if (semop(sem_id_, &op, 1) == -1) {
        LOG_ERROR("Failed to wait for semaphore %d: %s", sem_index, strerror(errno));
        return false;
    }
    
    return true;
}

bool IPCSharedMemory::SemaphoreSignal(int sem_index) {
    if (sem_id_ == -1) {
        LOG_ERROR("Invalid semaphore ID");
        return false;
    }
    
    struct sembuf op;
    op.sem_num = sem_index;
    op.sem_op = 1;   // Increment by 1
    op.sem_flg = 0;  // No special flags
    
    if (semop(sem_id_, &op, 1) == -1) {
        LOG_ERROR("Failed to signal semaphore %d: %s", sem_index, strerror(errno));
        return false;
    }
    
    return true;
}

} // namespace ipc
} // namespace util
