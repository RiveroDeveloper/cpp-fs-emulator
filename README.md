# Project Report: Copy-on-Write File System (COWFS)

## Abstract

This project has successfully developed a library that implements a file system with Copy-on-Write (COW) mechanism for efficient file version management. The COWFS library provides an alternative to traditional writing systems, allowing for maintaining a complete history of file versions while optimizing memory usage through advanced storage management techniques.

## Project Objectives

The project aimed to achieve the following objectives:

1. Implement a file system that uses the Copy-on-Write mechanism.
2. Provide automatic file versioning with each write operation.
3. Guarantee data integrity through preservation of previous versions.
4. Optimize memory usage through shared reference techniques and delta detection.
5. Offer a simple and familiar programming interface for developers.

## Implementation and Architecture

### Main Components

1. **File Management**: Implementation of basic operations (create, open, read, write, close).
2. **Versioning System**: Automatic version maintenance with each write operation.
3. **Storage Optimization**: Delta detection and block sharing.
4. **Memory Management**: Efficient block allocation and garbage collection.

### Copy-on-Write Mechanism

The COW principle was implemented so that file modifications never overwrite existing data. Instead, new versions are created that only store the differences (deltas) from previous versions, sharing unmodified blocks through a reference counter system.

### Key Algorithms

1. **Delta Detection**: Identifies exactly which parts of a file have changed between versions.
2. **Best-Fit**: Algorithm for block allocation that minimizes fragmentation.
3. **Free Block Merging**: Combines contiguous blocks to improve storage efficiency.
4. **Garbage Collection**: Identifies and frees blocks that are no longer referenced.

## Findings and Results

### Performance

- **Reading**: Read operations maintain performance similar to traditional systems, always accessing the latest available version.
- **Writing**: Write operations are more expensive due to the creation of new versions and delta detection, but the impact is minimized through storage optimization.
- **Rollback**: Returning to previous versions is efficient and does not require complete file reconstruction.

### Data Integrity

The system guarantees that no write operation can corrupt previous versions, providing a robust mechanism for:
- Error recovery
- Historical change analysis
- Reversion to known previous states

## Limitations and Challenges

During project development, the following limitations were identified:

1. **Memory Overhead**: Although optimized, the system requires more space than one without versioning.
2. **Implementation Complexity**: Reference management and delta detection increase code complexity.
3. **Scalability**: The system is designed for a limited number of files and a maximum disk size.
4. **Concurrency**: Advanced mechanisms for concurrent writes have not been implemented.
5. **Blocks**: The program is designed for files of only 4096 bytes, so if there is a file with more bytes, its versioning becomes difficult.

## Conclusions

The project has successfully met its objectives, implementing a functional COW file system that:

1. **Preserves History**: Automatically maintains a complete version history.
2. **Offers Flexibility**: Allows reverting to any previous version.
3. **Guarantees Integrity**: Ensures that data is never overwritten or corrupted.

The COWFS library demonstrates the practical applicability of virtual memory and file system concepts in creating efficient and robust data management tools.

## Learning Outcomes

This project has allowed us to apply and deepen knowledge in:

1. **Memory Virtualization**: Practical application of virtual memory management concepts.
2. **File Systems**: Implementation of advanced storage mechanisms.
3. **Resource Optimization**: Techniques for efficient storage utilization.
4. **Data Structures**: Use of complex structures for metadata and block management.

The COWFS library demonstrates how theoretical principles of operating systems can be applied to create practical solutions to real data management problems.
