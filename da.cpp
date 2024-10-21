#include <iostream>
#include <filesystem>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <cstring>
#include <fstream>
#include <sstream>
#include <set>
#include <limits>

namespace fs = std::filesystem;

// ANSI color codes for console output
const std::string RESET = "\033[0m";
const std::string RED = "\033[31m";
const std::string GREEN = "\033[32m";
const std::string YELLOW = "\033[33m";
const std::string BLUE = "\033[34m";
const std::string MAGENTA = "\033[35m";
const std::string CYAN = "\033[36m";

// Maximum memory limit for stats (100MB)
const size_t MAX_MEMORY_LIMIT = 100 * 1024 * 1024;

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

// Safe string to lowercase conversion
std::string safeToLower(const std::string &input)
{
    std::string result;
    for (unsigned char ch : input)
    {
        result += std::tolower(ch);
    }
    return result;
}

// Convert human-readable size to bytes with validation
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

// Structure to store statistics for each file type with safe arithmetic
struct FileTypeStats
{
    size_t count = 0;
    size_t totalSize = 0;

    void update(const size_t size)
    {
        if (count == std::numeric_limits<size_t>::max())
        {
            throw std::overflow_error("File count overflow");
        }
        count++;

        if (totalSize > std::numeric_limits<size_t>::max() - size)
        {
            throw std::overflow_error("Total size overflow");
        }
        totalSize += size;
    }
};

class FileAnalyzer
{
private:
    std::unordered_map<std::string, FileTypeStats> stats;
    size_t totalFiles = 0;
    size_t totalSize = 0;
    size_t hiddenFiles = 0;
    size_t hiddenSize = 0;
    std::vector<fs::path> excludeDirs;
    std::set<std::string> includeTypes;
    bool showHidden = false;
    SizeThreshold sizeThreshold;

    // Determine the file type based on its extension with proper handling
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
        ext = safeToLower(ext);

        // Handle dotfiles
        if (filename[0] == '.')
        {
            if (filename == ext)
            {
                return "[dotfile]";
            }
            return !ext.empty() ? ext : "[dotfile]";
        }

        return ext.empty() ? "[no extension]" : ext;
    }

    // Format file size with proper unit handling
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

    // Improved path exclusion check
    bool shouldExclude(const fs::path &path) const
    {
        try
        {
            for (const auto &dir : excludeDirs)
            {
                if (fs::equivalent(path, dir) ||
                    fs::equivalent(path.parent_path(), dir) ||
                    path.string().find(dir.string() + fs::path::preferred_separator) == 0)
                {
                    return true;
                }
            }
        }
        catch (const fs::filesystem_error &)
        {
            // If we can't check equivalence, fall back to string comparison
            std::string pathStr = path.string();
            for (const auto &dir : excludeDirs)
            {
                if (pathStr.find(dir.string() + fs::path::preferred_separator) == 0)
                {
                    return true;
                }
            }
        }
        return false;
    }

    // Check if a file type should be included
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
        if (str.find(',') != std::string::npos ||
            str.find('"') != std::string::npos ||
            str.find('\n') != std::string::npos)
        {
            std::string escaped = str;
            std::replace(escaped.begin(), escaped.end(), '"', '"');
            return "\"" + escaped + "\"";
        }
        return str;
    }

    // Calculate directory size
    size_t calculateDirectorySize(const fs::path &dir) const
    {
        size_t size = 0;
        for (const auto &entry : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied))
        {
            if (entry.is_regular_file())
            {
                size += entry.file_size();
            }
        }
        return size;
    }

public:
    FileAnalyzer(bool showHidden = false) : showHidden(showHidden) {}

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
        std::error_code ec;
        for (const auto &entry : fs::directory_iterator(path, fs::directory_options::skip_permission_denied, ec))
        {
            if (ec)
            {
                std::cerr << RED << "Warning: " << ec.message() << " at " << entry.path() << RESET << std::endl;
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
                    size_t gitSize = calculateDirectorySize(entry.path());
                    if (showHidden)
                    {
                        stats[".git"].update(gitSize);
                        totalFiles++;
                    }
                    else
                    {
                        hiddenFiles++;
                        hiddenSize += gitSize;
                    }
                    totalSize += gitSize;
                    continue;
                }
                analyze(entry.path()); // Recursive call for subdirectories
                continue;
            }

            if (entry.is_regular_file())
            {
                try
                {
                    size_t size = fs::file_size(entry.path(), ec);
                    if (ec)
                    {
                        std::cerr << RED << "Warning: Cannot get size of " << entry.path()
                                  << " - " << ec.message() << RESET << std::endl;
                        continue;
                    }

                    if (!isWithinSizeThreshold(size))
                    {
                        continue;
                    }

                    std::string fileType = getFileType(entry.path());
                    if (!shouldInclude(fileType))
                    {
                        continue;
                    }

                    if (entry.path().filename().string()[0] == '.' && !showHidden)
                    {
                        hiddenFiles++;
                        hiddenSize += size;
                    }
                    else
                    {
                        stats[fileType].update(size);
                        totalFiles++;
                    }

                    totalSize += size;
                }
                catch (const std::exception &e)
                {
                    std::cerr << RED << "Error processing file " << entry.path()
                              << ": " << e.what() << RESET << std::endl;
                    continue;
                }
            }
        }
    }

    void printResults() const
    {
        if (totalFiles == 0)
        {
            std::cout << RED << "No files found.\n"
                      << RESET;
            return;
        }

        std::vector<std::pair<std::string, FileTypeStats>> sorted(stats.begin(), stats.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto &a, const auto &b)
                  {
                      return a.second.totalSize > b.second.totalSize;
                  });

        // Print summary
        std::cout << "\n"
                  << CYAN << "+" << std::string(60, '-') << "+" << RESET << "\n";
        std::cout << CYAN << "| " << GREEN << std::left << std::setw(25) << "Total files: " + std::to_string(totalFiles)
                  << CYAN << " | " << GREEN << std::left << std::setw(30) << "Total size: " + formatSize(totalSize) << CYAN << " |" << RESET << "\n";
        std::cout << CYAN << "+" << std::string(60, '-') << "+" << RESET << "\n\n";

        // Print table header
        std::cout << CYAN << "+" << std::string(20, '-') << "+" << std::string(15, '-')
                  << "+" << std::string(20, '-') << "+" << RESET << "\n";
        std::cout << CYAN << "|" << YELLOW << std::setw(20) << std::left << " File Type"
                  << CYAN << "|" << YELLOW << std::setw(15) << std::right << "Count"
                  << CYAN << "|" << YELLOW << std::setw(20) << std::right << "Total Size"
                  << CYAN << "|" << RESET << "\n";
        std::cout << CYAN << "+" << std::string(20, '-') << "+" << std::string(15, '-')
                  << "+" << std::string(20, '-') << "+" << RESET << "\n";

        // Print table content
        for (const auto &[fileType, stat] : sorted)
        {
            std::cout << CYAN << "|" << GREEN << std::setw(20) << std::left << fileType
                      << CYAN << "|" << GREEN << std::setw(15) << std::right << stat.count
                      << CYAN << "|" << GREEN << std::setw(20) << std::right << formatSize(stat.totalSize)
                      << CYAN << "|" << RESET << "\n";
        }

        std::cout << CYAN << "+" << std::string(20, '-') << "+" << std::string(15, '-')
                  << "+" << std::string(20, '-') << "+" << RESET << "\n";

        if (!showHidden && hiddenFiles > 0)
        {
            std::cout << YELLOW << "\nHidden files: " << hiddenFiles
                      << " (Size: " << formatSize(hiddenSize) << ")" << RESET << "\n";
        }
    }

    void exportCsv(const std::string &filename) const
    {
        std::ofstream file(filename);
        if (!file)
        {
            throw std::runtime_error("Cannot create output file: " + filename);
        }

        file << "FileType,Count,TotalSize\n";
        for (const auto &[fileType, stat] : stats)
        {
            file << escapeCSV(fileType) << ","
                 << stat.count << ","
                 << stat.totalSize << "\n";
        }

        if (!showHidden && hiddenFiles > 0)
        {
            file << "Hidden files," << hiddenFiles << "," << hiddenSize << "\n";
        }

        std::cout << GREEN << "Results exported to " << filename << RESET << std::endl;
    }
};

void printUsage(const char *programName)
{
    std::cout << YELLOW << "Usage: " << programName << " [options] directory\n"
              << "Options:\n"
              << "  -h, --help           Show this help message\n"
              << "  -a, --all            Include hidden files\n"
              << "  -e, --exclude        Directory to exclude (can be used multiple times)\n"
              << "  -o, --output         Export results to CSV file\n"
              << "  -t, --type           File type to include (can be used multiple times)\n"
              << "  -s, --min-size       Minimum file size (e.g., 10K, 1M, 1.5G)\n"
              << "  -S, --max-size       Maximum file size (e.g., 100M, 2G)\n"
              << "Example:\n"
              << "  " << programName << " -a -e node_modules -t .cpp -t .h -s 1K -S 1M -o results.csv /path/to/dir\n"
              << RESET;
}

int main(int argc, char *argv[])
{
    std::string targetDir;
    std::string outputFile;
    bool showHidden = false;
    std::vector<std::string> excludeDirs;
    std::vector<std::string> includeTypes;
    SizeThreshold sizeThreshold;

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
        analyzer.setSizeThreshold(sizeThreshold);
        for (const auto &dir : excludeDirs)
        {
            analyzer.addExcludeDir(dir);
        }
        for (const auto &type : includeTypes)
        {
            analyzer.addIncludeType(type.empty() ? "[no extension]" : type);
        }
        std::cout << BLUE << "Analyzing directory: " << targetDir << RESET << std::endl;
        analyzer.analyze(targetDir);
        analyzer.printResults();
        if (!outputFile.empty())
        {
            analyzer.exportCsv(outputFile);
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << RED << e.what() << RESET << std::endl;
        printUsage(argv[0]);
        return 1;
    }
    return 0;
}