#include <iostream>
#include <filesystem>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <set>
#include <limits>
#include <thread>
#include <mutex>
#include <atomic>
#include <future>
#include <chrono>

namespace fs = std::filesystem;

// ANSI color codes for console output
struct Colors
{
    static const std::string RESET;
    static const std::string RED;
    static const std::string GREEN;
    static const std::string YELLOW;
    static const std::string BLUE;
    static const std::string CYAN;
};

const std::string Colors::RESET = "\033[0m";
const std::string Colors::RED = "\033[31m";
const std::string Colors::GREEN = "\033[32m";
const std::string Colors::YELLOW = "\033[33m";
const std::string Colors::BLUE = "\033[34m";
const std::string Colors::CYAN = "\033[36m";

// Structure to store file size threshold
struct SizeThreshold
{
    size_t minSize = 0;
    size_t maxSize = std::numeric_limits<size_t>::max();

    bool isValid() const
    {
        return minSize <= maxSize;
    }
};

// Structure to store statistics for each file type with thread-safe updates
struct FileTypeStats
{
    std::atomic<size_t> count{0};
    std::atomic<size_t> totalSize{0};

    void update(const size_t size)
    {
        count++;
        totalSize += size;
    }
};

// Non-atomic view of FileTypeStats for display purposes
struct FileTypeStatsView
{
    std::string fileType;
    size_t count;
    size_t totalSize;

    FileTypeStatsView(const std::string &type, const FileTypeStats &stats)
        : fileType(type), count(stats.count.load()), totalSize(stats.totalSize.load()) {}

    bool operator>(const FileTypeStatsView &other) const
    {
        return totalSize > other.totalSize;
    }
};

class FileAnalyzer
{
private:
    std::unordered_map<std::string, FileTypeStats> stats;
    std::mutex statsMutex;
    std::atomic<size_t> totalFiles{0};
    std::atomic<size_t> totalSize{0};
    std::atomic<size_t> hiddenFiles{0};
    std::atomic<size_t> hiddenSize{0};
    std::vector<fs::path> excludeDirs;
    std::set<std::string> includeTypes;
    bool showHidden = false;
    SizeThreshold sizeThreshold;
    unsigned int numThreads;

    // Cache for processed directories to avoid redundant work
    std::set<std::string> processedDirs;
    std::mutex processedDirsMutex;

    // Safe lowercase conversion
    static std::string toLower(const std::string &input)
    {
        std::string result;
        result.reserve(input.size());
        for (unsigned char ch : input)
        {
            result += static_cast<char>(std::tolower(ch));
        }
        return result;
    }

    // Determine file type based on extension
    static std::string getFileType(const fs::path &path)
    {
        if (path.empty())
        {
            return "[invalid]";
        }

        std::string filename = path.filename().string();
        if (filename.empty())
        {
            return "[invalid]";
        }

        std::string ext = path.extension().string();
        ext = toLower(ext);

        // Handle dotfiles
        if (!filename.empty() && filename[0] == '.')
        {
            if (filename == ext)
            {
                return "[dotfile]";
            }
            return !ext.empty() ? ext : "[dotfile]";
        }

        return ext.empty() ? "[no extension]" : ext;
    }

    // Format file size with proper unit
    static std::string formatSize(size_t bytes)
    {
        const char *units[] = {"B", "KB", "MB", "GB", "TB"};
        int unit = 0;
        double size = static_cast<double>(bytes);

        while (size >= 1024 && unit < 4)
        {
            size /= 1024;
            unit++;
        }

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << size << " " << units[unit];
        return oss.str();
    }

    // Check if path should be excluded
    bool shouldExclude(const fs::path &path) const
    {
        try
        {
            const std::string pathStr = path.string();
            for (const auto &dir : excludeDirs)
            {
                const std::string excludeStr = dir.string();
                if (pathStr == excludeStr ||
                    pathStr.find(excludeStr + fs::path::preferred_separator) == 0)
                {
                    return true;
                }

                try
                {
                    if (fs::equivalent(path, dir) || fs::equivalent(path.parent_path(), dir))
                    {
                        return true;
                    }
                }
                catch (const fs::filesystem_error &)
                {
                    // Fall back to string comparison already done above
                }
            }
        }
        catch (const std::exception &)
        {
            // If any error occurs, be conservative and don't exclude
        }
        return false;
    }

    // Check if file type should be included
    bool shouldInclude(const std::string &fileType) const
    {
        if (includeTypes.empty())
        {
            return true;
        }
        return includeTypes.find(fileType) != includeTypes.end();
    }

    // Validate file size against thresholds
    bool isWithinSizeThreshold(size_t size) const
    {
        return size >= sizeThreshold.minSize && size <= sizeThreshold.maxSize;
    }

    // Safe CSV field escaping
    static std::string escapeCSV(const std::string &str)
    {
        if (str.find_first_of(",\"\n\r") != std::string::npos)
        {
            std::string escaped = str;
            size_t pos = 0;
            while ((pos = escaped.find('"', pos)) != std::string::npos)
            {
                escaped.replace(pos, 1, "\"\"");
                pos += 2;
            }
            return "\"" + escaped + "\"";
        }
        return str;
    }

    // Process a single file
    void processFile(const fs::path &filePath)
    {
        std::error_code ec;
        try
        {
            size_t size = fs::file_size(filePath, ec);
            if (ec)
            {
                std::cerr << Colors::RED << "Warning: Cannot get size of " << filePath
                          << " - " << ec.message() << Colors::RESET << std::endl;
                return;
            }

            if (!isWithinSizeThreshold(size))
            {
                return;
            }

            std::string fileType = getFileType(filePath);
            if (!shouldInclude(fileType))
            {
                return;
            }

            totalSize += size;

            if (filePath.filename().string()[0] == '.' && !showHidden)
            {
                hiddenFiles++;
                hiddenSize += size;
            }
            else
            {
                // Thread-safe update of stats
                std::lock_guard<std::mutex> lock(statsMutex);
                stats[fileType].update(size);
                totalFiles++;
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << Colors::RED << "Error processing file " << filePath
                      << ": " << e.what() << Colors::RESET << std::endl;
        }
    }

    // Process a directory - can be called recursively or in separate threads
    void processDirectory(const fs::path &dirPath)
    {
        // Check if we've already processed this directory
        {
            std::lock_guard<std::mutex> lock(processedDirsMutex);
            std::string dirPathStr = dirPath.string();
            if (processedDirs.find(dirPathStr) != processedDirs.end())
            {
                return;
            }
            processedDirs.insert(dirPathStr);
        }

        std::error_code ec;
        std::vector<fs::path> subDirs;
        std::vector<fs::path> files;

        try
        {
            for (const auto &entry : fs::directory_iterator(dirPath, fs::directory_options::skip_permission_denied, ec))
            {
                if (ec)
                {
                    std::cerr << Colors::RED << "Warning: " << ec.message() << " at "
                              << entry.path() << Colors::RESET << std::endl;
                    ec.clear();
                    continue;
                }

                if (shouldExclude(entry.path()))
                {
                    continue;
                }

                if (entry.is_directory())
                {
                    if (entry.path().filename() == ".git")
                    {
                        std::error_code gitSizeEc;
                        size_t gitSize = 0;

                        try
                        {
                            for (const auto &gitEntry : fs::recursive_directory_iterator(
                                     entry.path(),
                                     fs::directory_options::skip_permission_denied,
                                     gitSizeEc))
                            {
                                if (gitEntry.is_regular_file())
                                {
                                    gitSize += fs::file_size(gitEntry.path(), gitSizeEc);
                                    if (gitSizeEc)
                                        gitSizeEc.clear();
                                }
                            }

                            totalSize += gitSize;

                            if (showHidden)
                            {
                                std::lock_guard<std::mutex> lock(statsMutex);
                                stats[".git"].update(gitSize);
                                totalFiles++;
                            }
                            else
                            {
                                hiddenFiles++;
                                hiddenSize += gitSize;
                            }
                        }
                        catch (const std::exception &)
                        {
                            // If we can't process the .git directory, just skip it
                        }
                    }
                    else
                    {
                        subDirs.push_back(entry.path());
                    }
                }
                else if (entry.is_regular_file())
                {
                    files.push_back(entry.path());
                }
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << Colors::RED << "Error accessing directory " << dirPath
                      << ": " << e.what() << Colors::RESET << std::endl;
        }

        // Process all files in this directory
        for (const auto &file : files)
        {
            processFile(file);
        }

        // Process subdirectories
        if (numThreads > 1 && subDirs.size() > 1)
        {
            std::vector<std::future<void>> futures;
            futures.reserve(subDirs.size());

            for (const auto &subDir : subDirs)
            {
                futures.push_back(std::async(std::launch::async,
                                             &FileAnalyzer::processDirectory, this, subDir));
            }

            // Wait for all tasks to complete
            for (auto &future : futures)
            {
                future.wait();
            }
        }
        else
        {
            // Sequential processing for single thread mode
            for (const auto &subDir : subDirs)
            {
                processDirectory(subDir);
            }
        }
    }

public:
    FileAnalyzer(bool showHidden = false)
        : showHidden(showHidden),
          numThreads(std::thread::hardware_concurrency() ? std::thread::hardware_concurrency() : 1) {}

    void setThreadCount(unsigned int threads)
    {
        numThreads = threads > 0 ? threads : 1;
    }

    void addExcludeDir(const std::string &dir)
    {
        excludeDirs.push_back(fs::path(dir));
    }

    void addIncludeType(const std::string &type)
    {
        includeTypes.insert(type.empty() ? "[no extension]" : type);
    }

    void setSizeThreshold(const SizeThreshold &threshold)
    {
        if (!threshold.isValid())
        {
            throw std::runtime_error("Invalid size threshold: min size must be less than or equal to max size");
        }
        sizeThreshold = threshold;
    }

    void analyze(const fs::path &path)
    {
        auto startTime = std::chrono::high_resolution_clock::now();

        // Clear previous analysis data
        stats.clear();
        totalFiles = 0;
        totalSize = 0;
        hiddenFiles = 0;
        hiddenSize = 0;
        processedDirs.clear();

        std::cout << Colors::BLUE << "Analyzing directory: " << path << Colors::RESET << std::endl;
        std::cout << Colors::BLUE << "Using " << numThreads << " threads" << Colors::RESET << std::endl;

        processDirectory(path);

        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
        std::cout << Colors::BLUE << "Analysis completed in " << duration / 1000.0 << " seconds" << Colors::RESET << std::endl;
    }

    void printResults() const
    {
        if (totalFiles == 0)
        {
            std::cout << Colors::RED << "No files found.\n"
                      << Colors::RESET;
            return;
        }

        // Convert stats to sortable vector using the non-atomic view
        std::vector<FileTypeStatsView> sorted;
        sorted.reserve(stats.size());
        for (const auto &[fileType, stat] : stats)
        {
            sorted.emplace_back(fileType, stat);
        }

        std::sort(sorted.begin(), sorted.end(),
                  [](const auto &a, const auto &b)
                  {
                      return a > b;
                  });

        // Print summary
        std::cout << "\n"
                  << Colors::CYAN << "+" << std::string(60, '-') << "+" << Colors::RESET << "\n";
        std::cout << Colors::CYAN << "| " << Colors::GREEN << std::left << std::setw(25)
                  << "Total files: " + std::to_string(totalFiles)
                  << Colors::CYAN << " | " << Colors::GREEN << std::left << std::setw(30)
                  << "Total size: " + formatSize(totalSize) << Colors::CYAN << " |" << Colors::RESET << "\n";
        std::cout << Colors::CYAN << "+" << std::string(60, '-') << "+" << Colors::RESET << "\n\n";

        // Print table header
        std::cout << Colors::CYAN << "+" << std::string(20, '-') << "+" << std::string(15, '-')
                  << "+" << std::string(20, '-') << "+" << Colors::RESET << "\n";
        std::cout << Colors::CYAN << "|" << Colors::YELLOW << std::setw(20) << std::left << " File Type"
                  << Colors::CYAN << "|" << Colors::YELLOW << std::setw(15) << std::right << "Count"
                  << Colors::CYAN << "|" << Colors::YELLOW << std::setw(20) << std::right << "Total Size"
                  << Colors::CYAN << "|" << Colors::RESET << "\n";
        std::cout << Colors::CYAN << "+" << std::string(20, '-') << "+" << std::string(15, '-')
                  << "+" << std::string(20, '-') << "+" << Colors::RESET << "\n";

        // Print table content
        for (const auto &statsView : sorted)
        {
            std::cout << Colors::CYAN << "|" << Colors::GREEN << std::setw(20) << std::left << statsView.fileType
                      << Colors::CYAN << "|" << Colors::GREEN << std::setw(15) << std::right << statsView.count
                      << Colors::CYAN << "|" << Colors::GREEN << std::setw(20) << std::right << formatSize(statsView.totalSize)
                      << Colors::CYAN << "|" << Colors::RESET << "\n";
        }

        std::cout << Colors::CYAN << "+" << std::string(20, '-') << "+" << std::string(15, '-')
                  << "+" << std::string(20, '-') << "+" << Colors::RESET << "\n";

        if (!showHidden && hiddenFiles > 0)
        {
            std::cout << Colors::YELLOW << "\nHidden files: " << hiddenFiles
                      << " (Size: " << formatSize(hiddenSize) << ")" << Colors::RESET << "\n";
        }
    }

    void exportCsv(const std::string &filename) const
    {
        std::ofstream file(filename);
        if (!file)
        {
            throw std::runtime_error("Cannot create output file: " + filename);
        }

        file << "FileType,Count,TotalSize,SizeHuman\n";
        for (const auto &[fileType, stat] : stats)
        {
            file << escapeCSV(fileType) << ","
                 << stat.count.load() << ","
                 << stat.totalSize.load() << ","
                 << escapeCSV(formatSize(stat.totalSize.load())) << "\n";
        }

        if (!showHidden && hiddenFiles > 0)
        {
            file << "Hidden files," << hiddenFiles << "," << hiddenSize << ","
                 << escapeCSV(formatSize(hiddenSize)) << "\n";
        }

        std::cout << Colors::GREEN << "Results exported to " << filename << Colors::RESET << std::endl;
    }
};

// Parse size string to bytes (e.g., "10KB" -> 10240)
size_t parseSize(const std::string &sizeStr)
{
    std::istringstream iss(sizeStr);
    double value;
    std::string unit;

    if (!(iss >> value) || value < 0)
    {
        throw std::runtime_error("Invalid size value: " + sizeStr);
    }
    iss >> unit;

    std::transform(unit.begin(), unit.end(), unit.begin(), ::toupper);

    if (unit == "B" || unit.empty())
        return static_cast<size_t>(value);
    if (unit == "KB" || unit == "K")
        return static_cast<size_t>(value * 1024);
    if (unit == "MB" || unit == "M")
        return static_cast<size_t>(value * 1024 * 1024);
    if (unit == "GB" || unit == "G")
        return static_cast<size_t>(value * 1024 * 1024 * 1024);
    if (unit == "TB" || unit == "T")
        return static_cast<size_t>(value * 1024 * 1024 * 1024 * 1024);

    throw std::runtime_error("Invalid size unit: " + unit);
}

void printUsage(const char *programName)
{
    std::cout << Colors::YELLOW << "Usage: " << programName << " [options] directory\n"
              << "Options:\n"
              << "  -h, --help           Show this help message\n"
              << "  -a, --all            Include hidden files\n"
              << "  -e, --exclude        Directory to exclude (can be used multiple times)\n"
              << "  -o, --output         Export results to CSV file\n"
              << "  -t, --type           File type to include (can be used multiple times)\n"
              << "  -s, --min-size       Minimum file size (e.g., 10K, 1M, 1.5G)\n"
              << "  -S, --max-size       Maximum file size (e.g., 100M, 2G)\n"
              << "  -j, --threads        Number of threads to use (default: all available cores)\n"
              << "Example:\n"
              << "  " << programName << " -a -e node_modules -t .cpp -t .h -s 1K -S 1M -j 4 -o results.csv /path/to/dir\n"
              << Colors::RESET;
}

auto main(int argc, char *argv[]) -> int
{
    std::string targetDir;
    std::string outputFile;
    bool showHidden = false;
    std::vector<std::string> excludeDirs;
    std::vector<std::string> includeTypes;
    SizeThreshold sizeThreshold;
    unsigned int threadCount = 0; // 0 means use default (all available cores)

    try
    {
        for (int i = 1; i < argc; i++)
        {
            std::string arg = argv[i];
            if (arg == "-h" || arg == "--help")
            {
                printUsage(argv[0]);
                return 0;
            }
            else if (arg == "-a" || arg == "--all")
            {
                showHidden = true;
            }
            else if (arg == "-e" || arg == "--exclude")
            {
                if (++i < argc)
                {
                    excludeDirs.push_back(argv[i]);
                }
                else
                {
                    throw std::runtime_error("Error: -e option requires a directory");
                }
            }
            else if (arg == "-o" || arg == "--output")
            {
                if (++i < argc)
                {
                    outputFile = argv[i];
                }
                else
                {
                    throw std::runtime_error("Error: -o option requires a filename");
                }
            }
            else if (arg == "-t" || arg == "--type")
            {
                if (++i < argc)
                {
                    includeTypes.push_back(argv[i]);
                }
                else
                {
                    throw std::runtime_error("Error: -t option requires a file type");
                }
            }
            else if (arg == "-s" || arg == "--min-size")
            {
                if (++i < argc)
                {
                    sizeThreshold.minSize = parseSize(argv[i]);
                }
                else
                {
                    throw std::runtime_error("Error: -s option requires a size value");
                }
            }
            else if (arg == "-S" || arg == "--max-size")
            {
                if (++i < argc)
                {
                    sizeThreshold.maxSize = parseSize(argv[i]);
                }
                else
                {
                    throw std::runtime_error("Error: -S option requires a size value");
                }
            }
            else if (arg == "-j" || arg == "--threads")
            {
                if (++i < argc)
                {
                    try
                    {
                        threadCount = std::stoi(argv[i]);
                        if (threadCount <= 0)
                        {
                            throw std::runtime_error("Thread count must be positive");
                        }
                    }
                    catch (const std::exception &)
                    {
                        throw std::runtime_error("Error: Invalid thread count: " + std::string(argv[i]));
                    }
                }
                else
                {
                    throw std::runtime_error("Error: -j option requires a number");
                }
            }
            else if (targetDir.empty())
            {
                targetDir = arg;
            }
        }

        if (targetDir.empty())
        {
            throw std::runtime_error("Error: No directory specified");
        }

        if (!fs::exists(targetDir) || !fs::is_directory(targetDir))
        {
            throw std::runtime_error("Error: Invalid directory: " + targetDir);
        }

        FileAnalyzer analyzer(showHidden);

        if (threadCount > 0)
        {
            analyzer.setThreadCount(threadCount);
        }

        analyzer.setSizeThreshold(sizeThreshold);

        for (const auto &dir : excludeDirs)
        {
            analyzer.addExcludeDir(dir);
        }

        for (const auto &type : includeTypes)
        {
            analyzer.addIncludeType(type.empty() ? "[no extension]" : type);
        }

        analyzer.analyze(targetDir);
        analyzer.printResults();

        if (!outputFile.empty())
        {
            analyzer.exportCsv(outputFile);
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << Colors::RED << e.what() << Colors::RESET << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    return 0;
}