#pragma once

#include "Common.h"
#include "db.h"

#include <boost/filesystem.hpp>
#include <boost/program_options/options_description.hpp>

namespace dev
{
namespace db
{
enum class DatabaseKind
{
    LevelDB,
    RocksDB,
    MemoryDB,
    Null
};
/// struct for db Version
struct DBVersion{
    std::string db_name ="null";
    int db_majorVersion =0;
    int db_minorVersion =0;
};
/// Provide a set of program options related to databases
///
/// @param _lineLength  The line length for description text wrapping, the same as in
///                     boost::program_options::options_description::options_description().
boost::program_options::options_description databaseProgramOptions(
    unsigned _lineLength = boost::program_options::options_description::m_default_line_length);

bool isDiskDatabase();
DatabaseKind databaseKind();
void setDatabaseKindByName(std::string const& _name);
void setDatabaseKind(DatabaseKind _kind);
boost::filesystem::path databasePath();
DatabaseKind stringtToKind(std::string _dbName);
std::vector<DBVersion> getDBVersion();

class DBFactory
{
public:
    DBFactory() = delete;
    ~DBFactory() = delete;

    static std::unique_ptr<DatabaseFace> create();
    static std::unique_ptr<DatabaseFace> create(boost::filesystem::path const& _path);
    static std::unique_ptr<DatabaseFace> create(DatabaseKind _kind);
    static std::unique_ptr<DatabaseFace> create(
        DatabaseKind _kind, boost::filesystem::path const& _path);

private:
};
}  // namespace db
}  // namespace dev