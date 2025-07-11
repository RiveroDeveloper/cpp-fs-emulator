#include "cowfs.hpp"
#include <fstream>
#include <cstring>
#include <stdexcept>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <algorithm>  

namespace cowfs {

COWFileSystem::COWFileSystem(const std::string& disk_path, size_t disk_size)
    : disk_path(disk_path), disk_size(disk_size), free_blocks_list(nullptr) {
    std::cout << "Initializing file system with size: " << disk_size << " bytes" << std::endl;
    
    total_blocks = disk_size / BLOCK_SIZE;
    std::cout << "Total blocks: " << total_blocks << std::endl;
    
    file_descriptors.resize(MAX_FILES);
    inodes.resize(MAX_FILES);
    blocks.resize(total_blocks);

    init_file_system();

    std::cout << "File system initialized with:" << std::endl
              << "  Max files: " << MAX_FILES << std::endl
              << "  Block size: " << BLOCK_SIZE << " bytes" << std::endl;

    // Inicializar la lista de bloques libres con todo el espacio disponible
    add_to_free_list(0, total_blocks);

    if (!initialize_disk()) {
        throw std::runtime_error("Failed to initialize disk");
    }
}

COWFileSystem::~COWFileSystem() {
    // Limpiar la lista de bloques libres
    while (free_blocks_list) {
        FreeBlockInfo* temp = free_blocks_list;
        free_blocks_list = free_blocks_list->next;
        delete temp;
    }


    std::ofstream disk(disk_path, std::ios::binary);
    if (disk.is_open()) {
        disk.write(reinterpret_cast<char*>(inodes.data()), inodes.size() * sizeof(Inode));
        disk.write(reinterpret_cast<char*>(blocks.data()), blocks.size() * sizeof(Block));
    }
}

bool COWFileSystem::initialize_disk() {
    std::ifstream disk(disk_path, std::ios::binary);
    if (disk.is_open()) {
        disk.read(reinterpret_cast<char*>(inodes.data()), inodes.size() * sizeof(Inode));
        disk.read(reinterpret_cast<char*>(blocks.data()), blocks.size() * sizeof(Block));
        return true;
    } else {
        std::ofstream new_disk(disk_path, std::ios::binary);
        if (!new_disk.is_open()) {
            return false;
        }

        for (auto& inode : inodes) {
            inode.is_used = false;
            inode.filename[0] = '\0';  
            inode.first_block = 0;
            inode.size = 0;
            inode.version_count = 0;
        }

        for (auto& block : blocks) {
            block.is_used = false;
            block.next_block = 0;
            std::memset(block.data, 0, BLOCK_SIZE);
        }

        new_disk.write(reinterpret_cast<char*>(inodes.data()), inodes.size() * sizeof(Inode));
        new_disk.write(reinterpret_cast<char*>(blocks.data()), blocks.size() * sizeof(Block));
        return true;
    }
}

fd_t COWFileSystem::create(const std::string& filename) {
    if (filename.length() >= MAX_FILENAME_LENGTH) {
        std::cerr << "Error: Filename too long" << std::endl;
        return -1;
    }

    if (find_inode(filename) != nullptr) {
        std::cerr << "Error: File already exists" << std::endl;
        return -1;
    }

    Inode* inode = nullptr;
    for (auto& i : inodes) {
        if (!i.is_used) {
            inode = &i;
            break;
        }
    }
    if (!inode) {
        std::cerr << "Error: No free inodes available" << std::endl;
        return -1;
    }

    std::memset(inode, 0, sizeof(Inode));
    std::strncpy(inode->filename, filename.c_str(), MAX_FILENAME_LENGTH - 1);
    inode->filename[MAX_FILENAME_LENGTH - 1] = '\0';
    inode->first_block = 0;
    inode->size = 0;
    inode->version_count = 0;  
    inode->is_used = true;
    inode->version_history.clear();

    fd_t fd = allocate_file_descriptor();
    if (fd < 0) {
        std::cerr << "Error: Failed to allocate file descriptor" << std::endl;
        inode->is_used = false;  
        return -1;
    }

    file_descriptors[fd].inode = inode;
    file_descriptors[fd].mode = FileMode::WRITE;
    file_descriptors[fd].current_position = 0;
    file_descriptors[fd].is_valid = true;

    std::cout << "Successfully created file with fd: " << fd << std::endl;
    return fd;
}

fd_t COWFileSystem::open(const std::string& filename, FileMode mode) {
    // Mostrar informacion de depuracion para ayudar a diagnosticar
    std::cout << "Attempting to open file '" << filename << "'" << std::endl;
    
    Inode* inode = find_inode(filename);
    if (!inode) {
        std::cerr << "File not found: " << filename << std::endl;
        return -1;
    }

    fd_t fd = allocate_file_descriptor();
    if (fd < 0) {
        std::cerr << "Failed to allocate file descriptor in open" << std::endl;
        return -1;
    }

    file_descriptors[fd].inode = inode;
    file_descriptors[fd].mode = mode;
    file_descriptors[fd].is_valid = true;

    // Para modo lectura, siempre empezamos al principio
    // Para modo escritura, podriamos empezar al final o al principio segun necesidades
    // Por ahora, para mantener compatibilidad, mantenemos escritura al final
    if (mode == FileMode::WRITE) {
        file_descriptors[fd].current_position = 0; // Cambiado a 0 para facilitar la escritura desde el inicio
    } else {
        file_descriptors[fd].current_position = 0;
    }

    std::cout << "Successfully opened file with fd: " << fd 
              << ", mode: " << (mode == FileMode::WRITE ? "WRITE" : "READ")
              << ", current_position: " << file_descriptors[fd].current_position 
              << std::endl;

    return fd;
}

ssize_t COWFileSystem::read(fd_t fd, void* buffer, size_t size) {
    if (fd < 0 || fd >= static_cast<fd_t>(file_descriptors.size()) || 
        !file_descriptors[fd].is_valid) {
        std::cerr << "Invalid file descriptor in read" << std::endl;
        return -1;
    }

    auto& fd_entry = file_descriptors[fd];
    if (!fd_entry.inode) {
        std::cerr << "No inode associated with file descriptor in read" << std::endl;
        return -1;
    }

    // Verificamos si el archivo esta vacio SOLO por su tamano, no por first_block
    // ya que first_block puede ser 0 (un indice valido)
    if (fd_entry.inode->size == 0) {
        std::cout << "read: Archivo vacio (tamano 0)" << std::endl;
        return 0;
    }
    
    // Verificar que el primer bloque sea valido (puede ser el bloque con indice 0)
    if (fd_entry.inode->first_block >= blocks.size() || 
        !blocks[fd_entry.inode->first_block].is_used) {
        std::cerr << "read: Primer bloque invalido o no usado: " 
                  << fd_entry.inode->first_block << std::endl;
        return -1;
    }

    // Calcular cuantos bytes leer basados en la posicion actual y el tamano del archivo
    size_t bytes_to_read = std::min(size, fd_entry.inode->size - fd_entry.current_position);
    if (bytes_to_read == 0) {
        std::cout << "read: Fin de archivo alcanzado (posicion actual: " 
                  << fd_entry.current_position << ", tamano: " << fd_entry.inode->size 
                  << ")" << std::endl;
        return 0;  // EOF
    }
    
    std::cout << "read: Leyendo " << bytes_to_read << " bytes desde la posicion " 
              << fd_entry.current_position << std::endl;
    std::cout << "read: Primer bloque: " << fd_entry.inode->first_block << std::endl;

    // Leer datos, navegando por la cadena de bloques
    size_t bytes_read = 0;
    size_t current_block = fd_entry.inode->first_block;
    size_t block_offset = fd_entry.current_position % BLOCK_SIZE;
    size_t blocks_skipped = fd_entry.current_position / BLOCK_SIZE;
    
    // Saltar bloques hasta llegar a la posicion actual
    for (size_t i = 0; i < blocks_skipped && current_block < blocks.size(); i++) {
        size_t next_block = blocks[current_block].next_block;
        // Si el siguiente bloque es 0 y no estamos en el ultimo bloque que necesitamos, 
        // consideramos esto como el fin de la cadena
        if (next_block >= blocks.size() && i < blocks_skipped - 1) {
            std::cerr << "read: Fin prematuro de la cadena de bloques al navegar" << std::endl;
            return -1;
        }
        current_block = next_block;
    }
    
    // Verificar si alcanzamos el final de la cadena de bloques
    if (current_block >= blocks.size() && bytes_to_read > 0) {
        std::cerr << "read: Error al saltar bloques para alcanzar la posicion actual" << std::endl;
        return -1;
    }
    
    // Leer datos
    while (bytes_read < bytes_to_read && current_block < blocks.size()) {
        // Verificar que el bloque este marcado como usado
        if (!blocks[current_block].is_used) {
            std::cerr << "Error: Attempted to read from unused block" << std::endl;
            return -1;
        }
        
        size_t chunk_size = std::min(bytes_to_read - bytes_read, BLOCK_SIZE - block_offset);
        
        std::cout << "read: Leyendo " << chunk_size << " bytes del bloque " 
                  << current_block << " con offset " << block_offset << std::endl;
        
        std::memcpy(static_cast<uint8_t*>(buffer) + bytes_read,
                   blocks[current_block].data + block_offset,
                   chunk_size);
        
        bytes_read += chunk_size;
        block_offset = 0; // Despues del primer bloque, siempre empezamos desde el inicio
        current_block = blocks[current_block].next_block;
    }

    // Actualizar la posicion actual
    fd_entry.current_position += bytes_read;
    
    std::cout << "read: Leidos " << bytes_read << " bytes, nueva posicion: " 
              << fd_entry.current_position << std::endl;
              
    return bytes_read;
}

std::string get_current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

bool COWFileSystem::find_delta(const void* old_data, const void* new_data,
                             size_t old_size, size_t new_size,
                             size_t& delta_start, size_t& delta_size) {
    const uint8_t* old_bytes = static_cast<const uint8_t*>(old_data);
    const uint8_t* new_bytes = static_cast<const uint8_t*>(new_data);
    
    // Si los datos son identicos, no hay delta
    if (old_size == new_size && std::memcmp(old_data, new_data, old_size) == 0) {
        delta_start = 0;
        delta_size = 0;
        return true;
    }
    
    // Encontrar donde comienzan las diferencias
    delta_start = 0;
    while (delta_start < old_size && delta_start < new_size &&
           old_bytes[delta_start] == new_bytes[delta_start]) {
        delta_start++;
    }
    
    // Si el nuevo contenido es mas corto y no hay diferencias hasta aqui
    if (delta_start == new_size && new_size < old_size) {
        delta_size = 0;
        return true;
    }
    
    // Si el nuevo contenido es mas largo pero igual hasta el final del viejo
    if (delta_start == old_size && new_size > old_size) {
        delta_size = new_size - old_size;
        return true;
    }
    
    // Encontrar donde terminan las diferencias desde el final
    size_t common_suffix = 0;
    while (common_suffix < (old_size - delta_start) && 
           common_suffix < (new_size - delta_start) &&
           old_bytes[old_size - 1 - common_suffix] == new_bytes[new_size - 1 - common_suffix]) {
        common_suffix++;
    }
    
    // Calcular el tamano del delta
    delta_size = (new_size - delta_start) - common_suffix;
    
    // Validacion final
    if (delta_start + delta_size > new_size) {
        delta_size = new_size - delta_start;
    }
    
    return true;
}

bool COWFileSystem::write_delta_blocks(const void* buffer, size_t size,
                                     size_t delta_start, size_t& first_block) {
    if (size == 0 || delta_start >= size) {
        first_block = 0;
        return true;
    }
    
    // Calcular cuantos bloques necesitamos
    size_t actual_size = std::min(size - delta_start, size);
    size_t blocks_needed = (actual_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    
    std::cout << "write_delta_blocks: Necesitamos " << blocks_needed 
              << " bloques para escribir " << actual_size << " bytes" << std::endl;
    
    first_block = 0;
    size_t current_block = 0;
    size_t prev_block = 0;
    
    const uint8_t* data = static_cast<const uint8_t*>(buffer) + delta_start;
    size_t remaining = actual_size;
    
    for (size_t i = 0; i < blocks_needed; i++) {
        if (!allocate_block(current_block)) {
            std::cerr << "write_delta_blocks: No se pudo asignar el bloque " << i+1 
                      << " de " << blocks_needed << std::endl;
            
            // Liberar los bloques que ya asignamos si fallamos
            if (first_block != 0) {
                size_t block_to_free = first_block;
                while (block_to_free != 0 && block_to_free < blocks.size()) {
                    size_t next = blocks[block_to_free].next_block;
                    free_block(block_to_free);
                    block_to_free = next;
                }
            }
            
            first_block = 0;
            return false;
        }
        
        // Si es el primer bloque, guardamos su indice
        if (i == 0) {
            first_block = current_block;
        } else {
            // Enlazar con el bloque anterior
            blocks[prev_block].next_block = current_block;
        }
        
        // Calcular cuantos bytes escribir en este bloque
        size_t bytes_to_write = std::min(remaining, BLOCK_SIZE);
        
        // Copiar los datos al bloque
        std::memcpy(blocks[current_block].data, data, bytes_to_write);
        
        // Inicializar el resto del bloque con ceros si es necesario
        if (bytes_to_write < BLOCK_SIZE) {
            std::memset(blocks[current_block].data + bytes_to_write, 0, BLOCK_SIZE - bytes_to_write);
        }
        
        data += bytes_to_write;
        remaining -= bytes_to_write;
        prev_block = current_block;
    }
    
    // Asegurar que el ultimo bloque tenga next_block = 0
    if (prev_block < blocks.size()) {
        blocks[prev_block].next_block = 0;
    }
    
    std::cout << "write_delta_blocks: Escritura exitosa en " << blocks_needed 
              << " bloques, primer bloque: " << first_block << std::endl;
    
    return true;
}

ssize_t COWFileSystem::write(fd_t fd, const void* buffer, size_t size) {
    std::cout << "Starting write operation for fd: " << fd << std::endl;
    
    if (fd < 0 || fd >= static_cast<fd_t>(file_descriptors.size()) || 
        !file_descriptors[fd].is_valid) {
        std::cerr << "Invalid file descriptor in write" << std::endl;
        return -1;
    }
    
    auto& fd_entry = file_descriptors[fd];
    if (fd_entry.mode != FileMode::WRITE) {
        std::cerr << "File not opened for writing" << std::endl;
        return -1;
    }
    
    if (!fd_entry.inode) {
        std::cerr << "No inode associated with file descriptor" << std::endl;
        return -1;
    }
    
    // Si el buffer esta vacio o el tamano es cero, no hacer nada
    if (!buffer || size == 0) {
        return 0;
    }
    
    // Obtener informacion del archivo actual
    size_t old_size = fd_entry.inode->size;
    size_t old_first_block = fd_entry.inode->first_block;
    
    // Para almacenar la informacion de los nuevos bloques
    size_t new_first_block = 0;
    size_t delta_start = 0;
    size_t delta_size = 0;
    
    // Determinar si es la primera version o necesitamos detectar cambios
    bool is_first_version = (fd_entry.inode->version_count == 0);
    
    if (is_first_version) {
        // Primera version, todo el contenido es nuevo
        delta_start = 0;
        delta_size = size;
    } else {
        // Leer el contenido actual para detectar cambios
        std::vector<uint8_t> old_content(old_size);
        
        if (old_size > 0) {
            // Guardar la posicion actual
            size_t saved_position = fd_entry.current_position;
            
            // Posicionar al inicio del archivo
            fd_entry.current_position = 0;
            
            // Leer el contenido actual
            ssize_t bytes_read = read(fd, old_content.data(), old_size);
            
            // Restaurar la posicion
            fd_entry.current_position = saved_position;
            
            // Verificar si la lectura tuvo exito
            if (bytes_read != static_cast<ssize_t>(old_size)) {
                std::cerr << "Error reading current content for delta detection" << std::endl;
                return -1;
            }
            
            // Detectar cambios entre versiones
            if (!find_delta(old_content.data(), buffer, old_size, size, delta_start, delta_size)) {
                std::cerr << "Error detecting delta between versions" << std::endl;
                return -1;
            }
        } else {
            // Si el archivo estaba vacio, todo es nuevo
            delta_start = 0;
            delta_size = size;
        }
    }
    
    // Si no hay cambios, no crear una nueva version
    if (delta_size == 0) {
        std::cout << "No changes detected, not creating a new version" << std::endl;
        
        // Pero si actualizamos la posicion del cursor
        fd_entry.current_position = size;
        
        return size;
    }
    
    // Crear una nueva cadena de bloques para la nueva version
    if (!write_delta_blocks(buffer, size, delta_start, new_first_block)) {
        std::cerr << "Could not allocate blocks for new version" << std::endl;
        return -1;
    }
    
    // Crear informacion de la nueva version
    VersionInfo new_version;
    new_version.version_number = fd_entry.inode->version_count + 1;
    new_version.timestamp = get_current_timestamp();
    new_version.size = size;
    new_version.block_index = new_first_block;
    new_version.delta_start = delta_start;
    new_version.delta_size = delta_size;
    new_version.prev_version = (fd_entry.inode->version_count > 0) ? fd_entry.inode->version_count : 0;
    
    // Incrementar la referencia a los nuevos bloques
    increment_block_refs(new_first_block);
    
    // Actualizar el inodo con la nueva informacion
    fd_entry.inode->version_history.push_back(new_version);
    fd_entry.inode->first_block = new_first_block;
    fd_entry.inode->size = size;
    fd_entry.inode->version_count++;
    
    // Actualizar la posicion del cursor
    fd_entry.current_position = size;

    std::cout << "Write operation completed:"
              << "\n  bytes written: " << size
              << "\n  delta size: " << delta_size
              << "\n  new version: " << fd_entry.inode->version_count
              << "\n  new size: " << fd_entry.inode->size
              << std::endl;
    
    return size;
}

int COWFileSystem::close(fd_t fd) {
    if (fd < 0 || fd >= static_cast<fd_t>(file_descriptors.size()) || 
        !file_descriptors[fd].is_valid) {
        return -1;
    }

    file_descriptors[fd].is_valid = false;
    return 0;
}

// Helper functions implementation
Inode* COWFileSystem::find_inode(const std::string& filename) {
    for (size_t i = 0; i < inodes.size(); i++) {
        if (inodes[i].is_used) {
            // Debug output to check what's happening
            std::cout << "Checking inode " << i << ": " 
                     << "used=" << inodes[i].is_used 
                     << ", filename='" << inodes[i].filename << "'" << std::endl;
            
            if (std::strcmp(inodes[i].filename, filename.c_str()) == 0) {
                return &inodes[i];
            }
        }
    }
    return nullptr;
}

fd_t COWFileSystem::allocate_file_descriptor() {
    for (size_t i = 0; i < file_descriptors.size(); ++i) {
        if (!file_descriptors[i].is_valid) {
            return static_cast<fd_t>(i);
        }
    }
    return -1;
}

void COWFileSystem::free_file_descriptor(fd_t fd) {
    if (fd >= 0 && fd < static_cast<fd_t>(file_descriptors.size())) {
        file_descriptors[fd].is_valid = false;
    }
}

bool COWFileSystem::allocate_block(size_t& block_index) {
    // Buscar el mejor bloque libre que se ajuste
    FreeBlockInfo* best_block = find_best_fit(1);
    
    if (!best_block) {
        std::cerr << "allocate_block: No hay bloques libres disponibles" << std::endl;
        std::cerr << "Memoria total: " << disk_size << " bytes" << std::endl;
        std::cerr << "Memoria usada: " << get_total_memory_usage() << " bytes" << std::endl;
        return false;
    }
    
    // Si llegamos aqui, encontramos un bloque libre
    std::cout << "allocate_block: Asignando bloque " << best_block->start_block << std::endl;
    
    block_index = best_block->start_block;
    
    // Actualizar la lista de bloques libres
    if (best_block->block_count > 1) {
        best_block->start_block++;
        best_block->block_count--;
    } else {
        // Este es el ultimo bloque de este grupo libre
        if (best_block == free_blocks_list) {
            free_blocks_list = best_block->next;
        } else {
            FreeBlockInfo* current = free_blocks_list;
            while (current != nullptr && current->next != best_block) {
                current = current->next;
            }
            if (current != nullptr) {
                current->next = best_block->next;
            }
        }
        delete best_block;
    }
    
    // Inicializar el bloque
    blocks[block_index].is_used = true;
    blocks[block_index].next_block = 0;
    blocks[block_index].ref_count = 0; // Se incrementara en increment_block_refs
    
    return true;
}

void COWFileSystem::free_block(size_t block_index) {
    if (block_index < blocks.size()) {
        blocks[block_index].is_used = false;
        blocks[block_index].next_block = 0;
    }
}

bool COWFileSystem::copy_block(size_t source_block, size_t& dest_block) {
    if (!allocate_block(dest_block)) {
        return false;
    }

    if (source_block != 0) {
        std::memcpy(blocks[dest_block].data, blocks[source_block].data, BLOCK_SIZE);
        blocks[dest_block].next_block = blocks[source_block].next_block;
    }

    return true;
}

void COWFileSystem::increment_block_refs(size_t block_index) {
    while (block_index != 0 && block_index < blocks.size()) {
        blocks[block_index].ref_count++;
        block_index = blocks[block_index].next_block;
    }
}

void COWFileSystem::decrement_block_refs(size_t block_index) {
    while (block_index != 0 && block_index < blocks.size()) {
        if (blocks[block_index].ref_count > 0) {
            blocks[block_index].ref_count--;
            if (blocks[block_index].ref_count == 0) {
                size_t next_block = blocks[block_index].next_block;
                free_block(block_index);
                block_index = next_block;
            } else {
                break; // Si aun hay referencias, no seguir
            }
        }
        block_index = blocks[block_index].next_block;
    }
}

// Version management implementation
std::vector<VersionInfo> COWFileSystem::get_version_history(fd_t fd) const {
    if (fd < 0 || fd >= static_cast<fd_t>(file_descriptors.size()) || 
        !file_descriptors[fd].is_valid) {
        std::cerr << "get_version_history: Invalid file descriptor: " << fd << std::endl;
        return std::vector<VersionInfo>();
    }
    
    if (!file_descriptors[fd].inode) {
        std::cerr << "get_version_history: No inode associated with file descriptor: " << fd << std::endl;
        return std::vector<VersionInfo>();
    }
    
    std::cout << "Retrieved version history for fd " << fd << ": " 
              << file_descriptors[fd].inode->version_history.size() << " versions" << std::endl;
    
    return file_descriptors[fd].inode->version_history;
}

size_t COWFileSystem::get_version_count(fd_t fd) const {
    if (fd < 0 || fd >= static_cast<fd_t>(file_descriptors.size()) || 
        !file_descriptors[fd].is_valid) {
        return 0;
    }
    return file_descriptors[fd].inode->version_count;
}

bool COWFileSystem::revert_to_version(fd_t fd, size_t version) {
    return false;
}

bool COWFileSystem::rollback_to_version(fd_t fd, size_t version_number) {
    std::cout << "Attempting rollback to version " << version_number << " for fd " << fd << std::endl;
    
    // Verificar que el descriptor de archivo sea valido
    if (fd < 0 || fd >= static_cast<fd_t>(file_descriptors.size()) || 
        !file_descriptors[fd].is_valid) {
        std::cerr << "Error: Invalid file descriptor for rollback" << std::endl;
        return false;
    }
    
    auto& fd_entry = file_descriptors[fd];
    if (!fd_entry.inode) {
        std::cerr << "Error: No inode associated with file descriptor for rollback" << std::endl;
        return false;
    }

    // Verificar que la version solicitada exista
    if (version_number == 0 || version_number > fd_entry.inode->version_count) {
        std::cerr << "Error: Version " << version_number << " does not exist (max: " << fd_entry.inode->version_count << ")" << std::endl;
        return false;
    }

    // Encontrar la version solicitada en el historial
    const VersionInfo* target_version = nullptr;
    for (const auto& v : fd_entry.inode->version_history) {
        if (v.version_number == version_number) {
            target_version = &v;
            break;
        }
    }
    
    if (!target_version) {
        std::cerr << "Error: Could not find version " << version_number << " in history" << std::endl;
        return false;
    }
    
    std::cout << "Rolling back to version " << target_version->version_number 
              << " with block index " << target_version->block_index 
              << " and size " << target_version->size << std::endl;

    // Guardar las versiones que vamos a mantener (hasta la version solicitada)
    std::vector<VersionInfo> kept_versions;
    for (const auto& v : fd_entry.inode->version_history) {
        if (v.version_number <= version_number) {
            kept_versions.push_back(v);
        } else {
            // Decrementar referencias para versiones que seran eliminadas
            if (v.block_index < blocks.size()) {
                std::cout << "Decrementing references for blocks of version " << v.version_number << std::endl;
                decrement_block_refs(v.block_index);
            }
        }
    }
    
    // Actualizar el inodo con la informacion de la version objetivo
    fd_entry.inode->version_history = kept_versions;
    fd_entry.inode->first_block = target_version->block_index;
    fd_entry.inode->size = target_version->size;
    fd_entry.inode->version_count = version_number;  // Actualizamos el contador de versiones
    
    // Actualizar la posicion actual en el descriptor de archivo
    // Para escritura, lo colocamos al final del archivo
    // Para lectura, lo dejamos como esta o lo reseteamos segun politica
    if (fd_entry.mode == FileMode::WRITE) {
        fd_entry.current_position = target_version->size;
    } else {
        fd_entry.current_position = 0; // Reset para lectura
    }
    
    std::cout << "Rollback completed successfully. New version count: " 
              << fd_entry.inode->version_count << std::endl;
    
    return true;
}

bool COWFileSystem::list_files(std::vector<std::string>& files) const {
    files.clear();
    for (const auto& inode : inodes) {
        if (inode.is_used) {
            files.push_back(inode.filename);
        }
    }
    return true;
}

size_t COWFileSystem::get_file_size(fd_t fd) const {
    if (fd < 0 || fd >= static_cast<fd_t>(file_descriptors.size()) || 
        !file_descriptors[fd].is_valid) {
        return 0;
    }
    return file_descriptors[fd].inode->size;
}

FileStatus COWFileSystem::get_file_status(fd_t fd) const {
    FileStatus status = {false, false, 0, 0};
    if (fd >= 0 && fd < static_cast<fd_t>(file_descriptors.size()) && 
        file_descriptors[fd].is_valid) {
        status.is_open = true;
        status.is_modified = (file_descriptors[fd].mode == FileMode::WRITE);
        status.current_size = file_descriptors[fd].inode->size;
        status.current_version = file_descriptors[fd].inode->version_count;
    }
    return status;
}

size_t COWFileSystem::get_total_memory_usage() const {
    size_t total = 0;
    for (const auto& block : blocks) {
        if (block.is_used) {
            total += BLOCK_SIZE;
        }
    }
    return total;
}

void COWFileSystem::garbage_collect() {
    std::vector<bool> block_used(blocks.size(), false);
    
    // Marcar bloques en uso
    for (const auto& inode : inodes) {
        if (inode.is_used) {
            for (const auto& version : inode.version_history) {
                size_t current_block = version.block_index;
                while (current_block != 0 && current_block < blocks.size()) {
                    if (blocks[current_block].ref_count > 0) {
                        block_used[current_block] = true;
                    }
                    current_block = blocks[current_block].next_block;
                }
            }
        }
    }
    
    // Encontrar bloques libres contiguos
    size_t start = 0;
    while (start < blocks.size()) {
        if (!block_used[start]) {
            size_t count = 0;
            while (start + count < blocks.size() && !block_used[start + count]) {
                blocks[start + count].is_used = false;
                blocks[start + count].next_block = 0;
                blocks[start + count].ref_count = 0;
                std::memset(blocks[start + count].data, 0, BLOCK_SIZE);
                count++;
            }
            
            if (count > 0) {
                add_to_free_list(start, count);
            }
            
            start += count;
        }
        start++;
    }
    
    merge_free_blocks();
}

void COWFileSystem::init_file_system() {
    // Initialize all file descriptors
    for (auto& fd : file_descriptors) {
        fd.inode = nullptr;
        fd.mode = FileMode::READ;
        fd.current_position = 0;
        fd.is_valid = false;
    }

    // Initialize all inodes
    for (auto& inode : inodes) {
        inode.is_used = false;
        std::memset(inode.filename, 0, MAX_FILENAME_LENGTH);
        inode.first_block = 0;
        inode.size = 0;
        inode.version_count = 0;
        inode.version_history.clear();
        inode.shared_blocks.clear();
    }

    // Initialize all blocks
    for (auto& block : blocks) {
        block.is_used = false;
        block.next_block = 0;
        block.ref_count = 0;
        std::memset(block.data, 0, BLOCK_SIZE);
    }
}

bool COWFileSystem::merge_free_blocks() {
    if (!free_blocks_list) return false;
    
    bool merged = false;
    FreeBlockInfo* current = free_blocks_list;
    
    while (current && current->next) {
        if (current->start_block + current->block_count == current->next->start_block) {
            // Fusionar bloques contiguos
            current->block_count += current->next->block_count;
            FreeBlockInfo* temp = current->next;
            current->next = current->next->next;
            delete temp;
            merged = true;
        } else {
            current = current->next;
        }
    }
    
    return merged;
}

bool COWFileSystem::split_free_block(FreeBlockInfo* block, size_t size_needed) {
    if (!block || block->block_count < size_needed) return false;
    
    if (block->block_count > size_needed) {
        // Crear nuevo bloque con el espacio restante
        FreeBlockInfo* new_block = new FreeBlockInfo{
            block->start_block + size_needed,
            block->block_count - size_needed,
            block->next
        };
        
        block->block_count = size_needed;
        block->next = new_block;
    }
    
    return true;
}

void COWFileSystem::add_to_free_list(size_t start, size_t count) {
    FreeBlockInfo* new_block = new FreeBlockInfo{start, count, nullptr};
    
    if (!free_blocks_list || start < free_blocks_list->start_block) {
        new_block->next = free_blocks_list;
        free_blocks_list = new_block;
    } else {
        FreeBlockInfo* current = free_blocks_list;
        while (current->next && current->next->start_block < start) {
            current = current->next;
        }
        new_block->next = current->next;
        current->next = new_block;
    }
    
    merge_free_blocks();
}

FreeBlockInfo* COWFileSystem::find_best_fit(size_t blocks_needed) {
    FreeBlockInfo* best_fit = nullptr;
    FreeBlockInfo* current = free_blocks_list;
    size_t smallest_difference = SIZE_MAX;
    
    while (current) {
        if (current->block_count >= blocks_needed) {
            size_t difference = current->block_count - blocks_needed;
            if (difference < smallest_difference) {
                smallest_difference = difference;
                best_fit = current;
                if (difference == 0) break; // Encontramos un ajuste perfecto
            }
        }
        current = current->next;
    }
    
    return best_fit;
}

} 