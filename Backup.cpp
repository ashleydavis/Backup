// http://stackoverflow.com/questions/3988128/c-thread-pool
// http://threadpool.sourceforge.net/

#include <boost/threadpool.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>
#include <boost/format.hpp>
#include <concurrent_queue.h>
#include <windows.h>
#include <string>
#include <iostream>
#include <fstream>
#include <vector>

#include "Timer.h"

static int NUM_THREADS = 8;

using namespace boost::threadpool;
using std::string;
namespace fs = boost::filesystem;
using std::cout;
using std::ifstream;
using std::ofstream;
using std::vector;

bool finished = false;

int filesCopied = 0;
int filesSkipped = 0;
int dirsExcluded = 0;
int filesExcluded = 0;
int errorsOccured = 0;
int nextThreadID = 0;
int tasksRemaining = 0;

struct Exclusions
{
    vector<string> excludedFileTypes;
    vector<string> excludedDirs;
};

struct BackupTask
{
    fs::path srcDir;
    fs::path backupDir;
    Exclusions* exclusions;
    bool parentEnableSkipChecks;
};

ofstream error_log("errors.log");

Concurrency::concurrent_queue<BackupTask> directoryQueue;
Concurrency::concurrent_queue<std::pair<fs::path, fs::path>> fileQueue;

boost::mutex outputMutex;

bool verbose = true;
bool logAllFiles = true;
bool logQueue = false;
bool showSkippedFiles = false;
bool logExclusions = true;
bool logThreads = true;

int BackupFiles(int threadID, const fs::path& srcDir, const fs::path& backupDir, Exclusions* exclusions, bool parentEnableSkipChecks);

void DisplayLastError() 
{ 
    // Retrieve the system error message for the last-error code

    LPVOID lpMsgBuf;
    DWORD dw = GetLastError(); 

    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | 
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        dw,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR) &lpMsgBuf,
        0, NULL );

    cout << "Last error: " << dw << ": " << lpMsgBuf << "\n";
    error_log << "Last error: " << dw << ": " << (const char*) lpMsgBuf << "\n";

    LocalFree(lpMsgBuf);
}

void backupTask()
{
    int threadID = ++nextThreadID;

    if (logThreads)
    {
        boost::unique_lock<boost::mutex> lock(outputMutex);

        cout << threadID << ": > Started thread.\n";
    }

    while (!finished)
    {
        std::pair<fs::path, fs::path> fileCopy;
        BackupTask task;
        if (fileQueue.try_pop(fileCopy))
        {
            if (logAllFiles)
            {
                boost::unique_lock<boost::mutex> lock(outputMutex);

                cout << threadID << ": > " << fileCopy.first << "\n";
            }

            if (!CopyFile(fileCopy.first.string().c_str(), fileCopy.second.string().c_str(), false))
            {
                {
                    boost::unique_lock<boost::mutex> lock(outputMutex);

                    cout << "Failed to copy file: " << fileCopy.first << "\n";
                    error_log << "Failed to copy file: " << fileCopy.first << "\n";

                    DisplayLastError();
                }


                ++errorsOccured;
            }
            else
            {
                ++filesCopied;
            }

            --tasksRemaining;
        }
        else if (directoryQueue.try_pop(task))
        {
            BackupFiles(threadID, task.srcDir, task.backupDir, task.exclusions, task.parentEnableSkipChecks);

            --tasksRemaining;
        }
        else
        {
            Sleep(1); // Wait a moment.
        }
    }

    if (logThreads)
    {
        boost::unique_lock<boost::mutex> lock(outputMutex);

        cout << threadID << ": > Thread finished.\n";
    }
}

#define _SECOND ((__int64) 10000000)
#define _MINUTE (60 * _SECOND)
#define _HOUR   (60 * _MINUTE)
#define _DAY    (24 * _HOUR)

int BackupFiles(int threadID, const fs::path& srcDir, const fs::path& backupDir, Exclusions* exclusions, bool parentEnableSkipChecks)
{
    int count = 0;

    auto dirBaseName = srcDir.filename();
    auto backupSubDir = backupDir / dirBaseName;
    bool enableSkipChecks = parentEnableSkipChecks;

    if (!fs::exists(backupSubDir))
    {
        // Create directory.
        enableSkipChecks = false;

        try 
        {
            fs::create_directory(backupSubDir);
        }
        catch (...)
        {
            cout << threadID << "> : " << "Failed to create directory: " << backupSubDir << "\n";

            return 0;
        }
    }

    // Progress message.
    {
        boost::unique_lock<boost::mutex> lock(outputMutex);

        cout << threadID << "> : " << srcDir << ", Tasks: " << tasksRemaining << "\n";
    }

    WIN32_FIND_DATA findData;
    fs::path wildCard = srcDir / "*.*";
    HANDLE h = FindFirstFile(wildCard.string().c_str(), &findData);
    if (h == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    do
    {
        if (strcmp(findData.cFileName, ".") == 0 ||
            strcmp(findData.cFileName, "..") == 0)
        {
            continue;
        }

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            fs::path subDir = srcDir / findData.cFileName;

            bool excluded = false;

            for (unsigned int i = 0; i < exclusions->excludedDirs.size(); ++i)
            {
                if (subDir.string().find(exclusions->excludedDirs[i]) != string::npos)
                {
                    // Directory excluded.
                    if (logExclusions)
                    {
                        boost::unique_lock<boost::mutex> lock(outputMutex);

                        cout << threadID << "> : Excluded dir: " << srcDir << "\n";
                    }

                    ++dirsExcluded;

                    excluded = true;

                    break;
                }
            }

            if (excluded)
            {
                continue; // Next file.
            }

            if (logQueue)
            {
                boost::unique_lock<boost::mutex> lock(outputMutex);

                cout << threadID << "++ Queueing DIR: " << subDir << "\n";
            }

            BackupTask backupTask = 
            {
                subDir,
                backupSubDir, 
                exclusions,
                enableSkipChecks,
            };
            directoryQueue.push(backupTask);

            ++tasksRemaining;
        }
        else
        {
            fs::path file = srcDir / findData.cFileName;

            bool excluded = false;

            for (unsigned int i = 0; i < exclusions->excludedFileTypes.size(); ++i)
            {
                if (file.extension() == exclusions->excludedDirs[i])
                {
                    // File excluded.
                    if (logExclusions)
                    {
                        boost::unique_lock<boost::mutex> lock(outputMutex);

                        cout << threadID << "> : Excluded file: " << file << "\n";
                    }

                    excluded = true;
                    break;
                }
            }

            if (excluded)
            {
                continue; // Next file.
           }

            fs::path destFile = backupSubDir / findData.cFileName;

            if (enableSkipChecks)
            {
                WIN32_FIND_DATA destFindData;
                HANDLE destH = FindFirstFile(destFile.string().c_str(), &destFindData);
                if (destH != INVALID_HANDLE_VALUE)
                {
                    LARGE_INTEGER srcFileSize;
                    srcFileSize.LowPart = findData.nFileSizeLow;
                    srcFileSize.HighPart = findData.nFileSizeHigh;

                    LARGE_INTEGER destFileSize;
                    destFileSize.LowPart = findData.nFileSizeLow;
                    destFileSize.HighPart = findData.nFileSizeHigh;

                    LARGE_INTEGER srcTime;
                    srcTime.LowPart = findData.ftLastWriteTime.dwLowDateTime;
                    srcTime.HighPart = findData.ftLastWriteTime.dwHighDateTime;

                    LARGE_INTEGER destTime;
                    destTime.LowPart = destFindData.ftLastWriteTime.dwLowDateTime;
                    destTime.HighPart = destFindData.ftLastWriteTime.dwHighDateTime;

                    //SYSTEMTIME srcSystemTime;
                    //FileTimeToSystemTime(&findData.ftLastWriteTime, &srcSystemTime); //fio:

                    //SYSTEMTIME destSystemTime;
                    //FileTimeToSystemTime(&destFindData.ftLastWriteTime, &destSystemTime); //fio:

                    __int64 intervalInSeconds = 3 * 60;


                    bool doTheCopy = false;

                    __int64 timeDiff100NsIntervals = srcTime.QuadPart - destTime.QuadPart;
                    __int64 timeDiffSeconds = timeDiff100NsIntervals / _SECOND; 

                    if (timeDiffSeconds > intervalInSeconds)
                    {
                        // Source file later than dest file.
                        // Copy it.
                        doTheCopy = true;
                    }
                    else if (srcFileSize.QuadPart != destFileSize.QuadPart)
                    {
                        // Different sizes.
                        // Copy it.
                        doTheCopy = true;
                    }

                    FindClose(destH);

                    if (!doTheCopy)
                    {
                        // Skip the file.
                        if (showSkippedFiles)
                        {
                            boost::unique_lock<boost::mutex> lock(outputMutex);

                            cout << threadID << "> : Skip file: " << file << "\n";

                        }

                        ++filesSkipped;

                        continue;
                    }
                }
            }

            if (logQueue)
            {
                boost::unique_lock<boost::mutex> lock(outputMutex);

                cout << threadID << "++ Queueing file: " << file << "\n";
            }

            fileQueue.push(std::pair<fs::path, fs::path>(file, destFile));

            ++tasksRemaining;

            ++count;
        }
    }
    while (FindNextFile(h, &findData) != 0);

    FindClose(h);

    return count;
}

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        cout << "Usage: backup <config-file>\n\n";
        cout << "Config file format: \n" 
             << "    v  -> Verbose mode.\n"
             << "    =  -> Set backup directory.\n"
             << "    +  -> Add included directory.\n"
             << "    -  -> Exclude a particular directory.\n"
             << "    >  -> Commit the backup.\n";
        return 1;;
    }

    const char* configFilename = argv[1];
    cout << "Reading config file: " << configFilename << "\n";

    // Create a thread pool.
    pool tp(NUM_THREADS);

    for (int i = 0; i < NUM_THREADS; ++i)
    {
        tp.schedule(&backupTask);
    }

    string backupDir;

    Timer timer;
    timer.Start();

    ifstream file(configFilename);

    if (file.fail())
    {
        cout << "Failed to open config file: " << configFilename << ".\n";
        return 1;
    }

    vector<string> includedDirs;
    Exclusions* exclusions = new Exclusions();

    int backupsCommitted = 0;
    int startupErrors = 0;

    std::string s;
    while (std::getline(file, s))
    {
        if (s[0] == 'v')
        {
            verbose = true;
        }
        else if (s[0] == '=')
        {
            backupDir = s.substr(1);

            if (!fs::exists(backupDir))
            {
                ++startupErrors;

                cout << "Error: Backup destination doesn't exist: " << backupDir << "\n";
            }
        }
        else if (s[0] == '+')
        {
            string includedDir = s.substr(1);

            includedDirs.push_back(includedDir);

            if (!fs::exists(includedDir))
            {
                ++startupErrors;

                cout << "Error: Backup source doesn't exist: " << backupDir << "\n";
            }
        }
        else if (s[0] == '-')
        {
            string excludedDir = s.substr(1);

            exclusions->excludedDirs.push_back(excludedDir);

            if (!fs::exists(excludedDir))
            {
                ++startupErrors;

                cout << "Error: Excluded directory doesn't exist: " << excludedDir << "\n";
            }
        }
        else if (s[0] == '!')
        {
            exclusions->excludedFileTypes.push_back(s.substr(1));
        }
        else if (s[0] == '>')
        {
            if (startupErrors > 0)
            {
                continue; // Don't attempt to do anything if there are errors.
            }

            ++backupsCommitted;

            for (unsigned int i = 0; i < includedDirs.size(); ++i)
            {
                BackupTask task =
                {
                    includedDirs[i],
                    backupDir,
                    exclusions,
                    true,
                };
                directoryQueue.push(task);

                ++tasksRemaining;
            }

            exclusions = new Exclusions(); // Small memory leak, but who cares.
        }
    }

    if (tasksRemaining > 0)
    {
        boost::unique_lock<boost::mutex> lock(outputMutex);

        cout << "Done, waiting for " << tasksRemaining << " tasks to complete.\n";
    }

    while (tasksRemaining > 0)
    {
        Sleep(1000);
    }

    finished = true;

    if (startupErrors > 0)
    {
        cout << "Encountered " << startupErrors << " start up errors." << "\n";
    }

    if (backupsCommitted == 0)
    {
        cout << "Error: no backups committed!\n";
        return 1;
    }

    double elapsedTime = timer.GetElapsedTime();

    cout << "Finished, no tasks remaining!\n";

    cout << "Files copied:   " << filesCopied << "\n";
    cout << "Files skipped:  " << filesSkipped << "\n";
    cout << "Dirs excluded:  " << dirsExcluded << "\n";
    cout << "Files excluded: " << filesExcluded << "\n";
    cout << "Errors:         " << errorsOccured << "\n";
    cout << "Time:           " << elapsedTime << "\n";

    return 0;
}




