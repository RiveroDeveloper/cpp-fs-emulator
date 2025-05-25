# COWFS Library API Documentation and Usage

## Introduction

The COWFS (Copy-on-Write File System) library implements a file system that uses the Copy-on-Write mechanism to manage writes and file versioning. This system guarantees data integrity and allows access to the version history of each file.

## Main Features

- **Automatic versioning**: Each write operation creates a new version of the file.
- **Optimized reads**: Read operations always access the latest available version.
- **Efficient memory management**: Uses reference techniques and garbage collection to minimize storage overhead.
- **Version rollback**: Allows reverting to previous versions of a file.
- **Delta detection**: Stores only changes between versions to optimize space usage.

## Library Structure

### Main Constants

- `BLOCK_SIZE`: 4096 bytes (size of each data block)
- `MAX_FILENAME_LENGTH`: 255 characters (maximum filename length)
- `MAX_FILES`: 1024 (maximum number of files in the system)

### Data Types

- `fd_t`: Type for file descriptors (int32_t)
- `FileMode`: Enumeration for file opening modes
  - `READ`: Read mode
  - `WRITE`: Write mode
  - `CREATE`: Create mode

### Data Structures

- `Block`: Represents a data block in the file system
- `VersionInfo`: Stores information about file versions
- `Inode`: Represents file metadata
- `FileStatus`: Current status of a file

## Internal Implementation

### Copy-on-Write (COW) Mechanism

The COWFS system implements the Copy-on-Write principle as follows:

1. **Initial write**: When a file is created for the first time, blocks are allocated to store its data.

2. **Subsequent writes**: When a file is modified:
   - Modified parts (deltas) are detected between the previous version and the new one
   - New blocks are created only to store the modified data
   - Unmodified blocks are shared with previous versions through reference counters
   - A record is created in the version history with metadata about the changes

3. **Shared references**: Each block has a reference counter that indicates how many versions use it. When a counter reaches zero, the block can be freed by the garbage collector.

### Storage Optimization

The system employs several techniques to minimize memory usage:

1. **Delta detection**: Only differences between consecutive versions are stored.
2. **Block sharing**: Unmodified blocks are shared between versions.
3. **Garbage collection**: Blocks no longer in use are periodically freed.
4. **Block-based memory management**: A memory allocation system based on fixed-size blocks is used.
5. **Best-fit algorithm**: For block allocation, an algorithm that minimizes fragmentation is used.

### Internal Structures

#### Free Block List
The system maintains a linked list of free blocks that is managed with the following operations:
- Merging contiguous blocks (`merge_free_blocks`)
- Splitting blocks to fit specific sizes (`split_free_block`)
- Finding the best-fit block according to required size (`find_best_fit`)

## Public API

### Main Functions

#### Constructor

```cpp
COWFileSystem(const std::string& disk_path, size_t disk_size)
```

Initializes the file system at the specified path with the indicated size.

- **Parameters**:
  - `disk_path`: Path of the file representing the disk
  - `disk_size`: Total disk size in bytes

#### Destructor

```cpp
~COWFileSystem()
```

Frees resources and saves the current state to disk.

#### Basic File Operations

##### Create a File

```cpp
fd_t create(const std::string& filename)
```

Creates a new file in the system.

- **Parameters**:
  - `filename`: Name of the file to create
- **Return**: Descriptor of the created file, or -1 on error

##### Open a File

```cpp
fd_t open(const std::string& filename, FileMode mode)
```

Opens an existing file in the system.

- **Parameters**:
  - `filename`: Name of the file to open
  - `mode`: Opening mode (READ, WRITE, CREATE)
- **Return**: Descriptor of the opened file, or -1 on error

##### Read from a File

```cpp
ssize_t read(fd_t fd, void* buffer, size_t size)
```

Reads data from an open file.

- **Parameters**:
  - `fd`: File descriptor
  - `buffer`: Buffer where the read data will be stored
  - `size`: Number of bytes to read
- **Return**: Number of bytes read, or -1 on error

##### Write to a File

```cpp
ssize_t write(fd_t fd, const void* buffer, size_t size)
```

Writes data to an open file, creating a new version.

- **Parameters**:
  - `fd`: File descriptor
  - `buffer`: Buffer with data to write
  - `size`: Number of bytes to write
- **Return**: Number of bytes written, or -1 on error

##### Close a File

```cpp
int close(fd_t fd)
```

Closes an open file.

- **Parameters**:
  - `fd`: Descriptor of the file to close
- **Return**: 0 if closed correctly, -1 on error

#### Version Management

##### Get Version History

```cpp
std::vector<VersionInfo> get_version_history(fd_t fd)
```

Gets the version history of a file.

- **Parameters**:
  - `fd`: File descriptor
- **Return**: Vector with information from all versions

##### Count Versions

```cpp
size_t get_version_count(fd_t fd)
```

Gets the total number of versions of a file.

- **Parameters**:
  - `fd`: File descriptor
- **Return**: Number of versions

##### Revert to a Previous Version

```cpp
bool revert_to_version(fd_t fd, size_t version)
```

Temporarily reverts to a previous version without deleting more recent versions.

- **Parameters**:
  - `fd`: File descriptor
  - `version`: Version number to revert to
- **Return**: true if reverted correctly, false on error

##### Rollback to a Previous Version

```cpp
bool rollback_to_version(fd_t fd, size_t version_number)
```

Permanently reverts to a previous version, deleting all subsequent versions.

- **Parameters**:
  - `fd`: File descriptor
  - `version_number`: Version number to rollback to
- **Return**: true if rollback was successful, false on error

#### File System Operations

##### List Files

```cpp
bool list_files(std::vector<std::string>& files)
```

Gets a list of all files in the system.

- **Parameters**:
  - `files`: Vector where file names will be stored
- **Return**: true if operation was successful, false on error

##### Get File Size

```cpp
size_t get_file_size(fd_t fd)
```

Gets the current size of a file.

- **Parameters**:
  - `fd`: File descriptor
- **Return**: File size in bytes

##### Get File Status

```cpp
FileStatus get_file_status(fd_t fd)
```

Gets the current status of a file.

- **Parameters**:
  - `fd`: File descriptor
- **Return**: Structure with file status

#### Memory Management

##### Get Total Memory Usage

```cpp
size_t get_total_memory_usage()
```

Calculates and returns the total memory usage of the file system.

- **Return**: Total memory usage in bytes

##### Garbage Collection

```cpp
void garbage_collect()
```

Executes the garbage collector to free unused blocks.

## Tips for Efficient Usage

1. **Proper file closure**: Always close files after using them to ensure changes are saved correctly.

2. **Version management**: Perform `rollback_to_version` only when necessary, as it permanently deletes subsequent versions.

3. **Using garbage collection**: Run `garbage_collect()` periodically when the system is not under intense load to free resources.

4. **Write sizes**: Try to group small modifications into larger write operations to minimize fragmentation.

5. **Available memory**: Monitor memory usage with `get_total_memory_usage()` to avoid exhausting it.

## Advanced Implementation

### Rollback Procedure

The process of rolling back to a previous version involves:

1. Verify that the requested version exists
2. Delete all versions subsequent to the requested one
3. Decrement reference counters of blocks exclusive to deleted versions
4. Update file metadata to reflect the state of the selected version

### Garbage Collection

The garbage collector algorithm:

1. Identifies all blocks currently in use
2. Marks as free blocks with reference counter equal to zero
3. Combines contiguous free blocks to reduce fragmentation
4. Updates the free block list

## Basic Usage Example

```cpp
#include "cowfs.hpp"
#include <iostream>

int main() {
    // Initialize file system
    cowfs::COWFileSystem fs("my_disk.dat", 1024 * 1024);  // 1MB

    // Create a file
    cowfs::fd_t fd = fs.create("example.txt");
    if (fd < 0) {
        std::cerr << "Error creating file" << std::endl;
        return 1;
    }

    // Write data (creates version 1)
    const char* data1 = "Hello, world!";
    fs.write(fd, data1, strlen(data1));

    // Close file
    fs.close(fd);

    // Reopen for reading
    fd = fs.open("example.txt", cowfs::FileMode::READ);
    
    // Read content
    char buffer[100] = {0};
    fs.read(fd, buffer, 100);
    std::cout << "File content: " << buffer << std::endl;

    // Close file
    fs.close(fd);

    return 0;
}
```

## Performance Considerations
 
- The system is optimized to minimize storage usage through delta detection between versions.
- Garbage collection should be run periodically to free unused blocks.
- Write operations are more expensive than read operations due to the creation of new versions.

## Limitations

- Maximum file size limited by total disk size.
- Maximum number of files defined by the MAX_FILES constant (1024).
- Not recommended for systems with high write concurrency.
- Not recommended for file versions larger than 4096 bytes