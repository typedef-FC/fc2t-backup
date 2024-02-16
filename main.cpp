/**
 * @title fc2-backup
 * @file main.cpp
 * @author typedef
 * @brief automatically archives your FC2 Sessions folder to a .zip file. backup your data in the future.
 */
#include <filesystem> /** std::filesystem **/
#include <zip.h> /** https://libzip.org/ **/
#include <format> /** std::format **/
#include <algorithm> /** std::find templates **/

#define FC2_TEAM_UNIVERSE4
#include <fc2.hpp> /** fc2t **/

namespace directories
{
    /**
     * @brief fc2 always dumps FC2T projects inside of your sessions/fc2t folder. instead of doing all sorts of work with fc2::call, let's just go up one directory instead.
     */
    std::filesystem::path sessions;
    std::filesystem::path archives;

    std::filesystem::path today;
    std::filesystem::path now;
}

int main( )
{
    /**
     * @brief get member session information. this is a relatively new function
     *
     * previous code (windows):
     * @code
            //
            // @brief windows is essentially outdated as per usual when it comes to anything related to files. if you execute this program using a .bat file as an administrator, 'current_path' will return the same directory as your cmd.exe. obviously this is not ideal.
            //
            // that doesn't seem like a practical situation. but in tradition to the unpredictability of windows versions, this should be considered at least.
            //
            // so, we will have to use the windows API instead.
            //
            //
            #ifdef _WIN32
                char dir_buffer[ MAX_PATH ];
                GetModuleFileNameA( nullptr, dir_buffer, MAX_PATH );
                directories::sessions = std::filesystem::path(dir_buffer).parent_path();
            #endif
     * @endcode
     *
     * linux:
     * @code
     *      std::filesystem::path sessions = std::filesystem::current_path();
     * @endcode
     */
    auto session = fc2::get_session();
    if( !strlen( session.directory ) )
    {
        std::fprintf( stderr, "fantasy.universe4 is not open" );
        return 1;
    }

    directories::sessions = std::filesystem::path{ session.directory };
    directories::archives = directories::sessions / BACKUP_DIRECTORY_NAME;

    /**
     * @brief get zip file for today
     */
    const auto now = std::chrono::system_clock::to_time_t( std::chrono::system_clock::now( ) );
    const auto tm = std::localtime( &now );

    std::ostringstream oss_today;
    oss_today << std::put_time( tm, BACKUP_ZIP_FILE_FORMAT );
    directories::today = directories::archives / oss_today.str();

    /**
     * @brief get directory for now
     */
    std::ostringstream oss_now;
    oss_now << std::put_time( tm, BACKUP_ZIP_FILE_NOW_FORMAT );
    directories::now = directories::archives / oss_now.str();

    /**
     * @brief assure directory locations are correct from a user standpoint.
     */
    std::printf(
            "sessions directory: %s\narchives directory: %s\ntoday's .zip: %s\nnow's .zip: %s",
            directories::sessions.string().c_str(),
            directories::archives.string().c_str(),
            directories::today.string().c_str(),
            directories::now.string().c_str()
    );

    /**
     * @brief create archives directory
     */
    if( !std::filesystem::exists( directories::archives ) )
    {
        if ( !std::filesystem::create_directory( directories::archives ))
        {
            std::fprintf( stderr, "failed to create directory\n" );
            return 1;
        }

        std::printf( "archives directory created\n" );
    }

    /**
     * @brief the plan is to create an archive based on the month, day and year. but per launch, an archive is created for each hour. basically:
     *
     *
     * @code
     *  2024-02-13.zip
     *      -> 13.zip (1 PM)
     *      -> 14.zip (2 PM)
     *          -> constellation4
     *              -> scripts
     *              -> logs
     *              -> core
     *          -> universe4
     *              -> scripts
     *              -> logs
     *              -> core
     * @endcode
     *
     *  etc...
     */
    int err; auto now_zip = zip_open( directories::now.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err );
    if ( !now_zip )
    {
        std::fprintf( stderr, "failed to create zip file (%d)\n", err );
        return 1;
    }

    /**
     * @brief loop through sessions directory. below is a list of directories to not archive
     */
    std::array< std::string, 2 > blacklist = {
            "archives", // dont archive ourselves
            "fc2t", // ignore fc2t folder
    };
    for( const auto & entry : std::filesystem::recursive_directory_iterator( directories::sessions ) )
    {
        const auto path = std::filesystem::relative( entry.path(), directories::sessions );

        static auto is_blacklisted = [&](const auto & dir)
        {
            auto p = dir;
            while (!p.empty())
            {
                if (std::find(blacklist.begin(), blacklist.end(), p.filename().string()) != blacklist.end())
                {
                    return true;
                }

                p = p.parent_path();
            }

            return false;
        };

        /**
        * @brief ignore the blacklisted directories
        */
        if( is_blacklisted( path ) )
        {
            continue;
        }

        /**
         * @brief this should avoid everything in your Sessions directory that isn't already sub-directories. we want to avoid storing everything in the Sessions directory, yet we want everything in the sub-directories.
         */
        if( directories::sessions == entry.path().parent_path().string() )
        {
            continue;
        }

        /**
         * @brief we found a sub-directory
         */
        if(entry.is_directory())
        {


            /**
             * @brief add the directory inside the zip
             */
            if( zip_dir_add( now_zip, path.string().c_str( ), ZIP_FL_ENC_GUESS ) < 0 )
            {
                std::fprintf( stderr, "failed to add directory \"%s\" into zip: %s\n", path.string().c_str(), zip_strerror( now_zip ) );
                return 1;
            }
        }
        else
        {
            /**
             * @brief add file to directory inside the .zip
             */
            struct zip_source * s;
            if ( !( s = zip_source_file( now_zip, entry.path().string().c_str(), 0, 0 ) ) || zip_file_add( now_zip, path.string().c_str(), s, ZIP_FL_ENC_GUESS) < 0 )
            {
                std::fprintf( stderr, "failed to add file \"%s\" into zip: %s\n", entry.path().string().c_str(), zip_strerror( now_zip ) );
                zip_source_free(s);
                return 1;
            }
        }
    }

    /**
     * @brief we're done with the .zip file now. going back to the map above...
     *
     * we're at a point where we created the .zip that contains the hour name. we just need to add it to today's .zip
     */
    zip_close( now_zip );

    /**
     * @brief open another .zip and put our recently generated .zip file inside of today's .zip
     */
    auto today_zip = zip_open( directories::today.string().c_str(), ZIP_CREATE, &err );
    if ( !today_zip )
    {
        std::fprintf( stderr, "failed to create zip file (%d)\n", err );
        return 1;
    }

    struct zip_source * s;
    if ( !( s = zip_source_file( now_zip, directories::now.string().c_str(), 0, 0 ) ) || zip_file_add( today_zip, oss_now.str().c_str(), s, ZIP_FL_ENC_GUESS) < 0 )
    {
        std::fprintf( stderr, "failed to add file \"%s\" into zip: %s\n", directories::now.string().c_str(), zip_strerror( today_zip ) );
        zip_source_free(s);
        return 1;
    }

    /**
     * @brief closing out
     */
    zip_close( today_zip );
    return 0;
}
