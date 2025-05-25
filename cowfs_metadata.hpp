#ifndef COWFS_METADATA_HPP
#define COWFS_METADATA_HPP

#include "cowfs.hpp"
#include <string>

namespace cowfs {

class MetadataManager {
public:

    static bool save_and_print_metadata(COWFileSystem& fs, const std::string& version_label);
    

    static void print_metadata(COWFileSystem& fs);
    

    static bool save_metadata(COWFileSystem& fs, const std::string& version_label);

private:

    static std::string generate_metadata_json(COWFileSystem& fs);
};

} 

#endif // COWFS_METADATA_HPP 