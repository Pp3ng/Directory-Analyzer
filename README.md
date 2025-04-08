# Directory Analyzer

Directory Analyzer is a C++ command-line tool designed to provide insights into directory structures and file system compositions. It offers a detailed breakdown of file types, sizes, and counts, making it useful for users who need to understand their file system organization.Directory Analyzer aims to provide a balance between simplicity and functionality, offering useful insights without overwhelming users with unnecessary complexity.

## Key Features

- **Deep Recursive Analysis**: Traverse through complex directory structures to provide a complete picture of your file system.
- **Intelligent File Categorization**: Automatically categorize files based on their extensions, offering a clear overview of file type distribution.
- **Advanced Filtering Capabilities**:
  - Size-based filtering to focus on files within specific size ranges
  - Type-based filtering to analyze only certain file types
  - Option to include or exclude hidden files
- **Directory Exclusion**: Ability to exclude specific directories from analysis, useful for ignoring irrelevant or system directories.
- **Detailed Statistics**: Provide comprehensive statistics including file counts, total sizes, average sizes, and size ranges for each file type and overall.
- **CSV Export**: Export detailed analysis results to CSV format for further processing or reporting.
- **User-Friendly Console Output**: Colorized and well-formatted console output for easy reading and interpretation of results.
- **Efficient Performance**: Optimized for speed and low resource usage, capable of handling large directory structures.
- **Multithreaded Processing**: Parallel directory scanning for faster analysis of large file systems.

## Use Cases

- Identifying large files or folders consuming disk space
- Analyzing project structures in development environments
- Auditing file type distributions in data storage systems
- Preparing reports on file system usage and composition

Directory Analyzer aims to provide a balance between simplicity and functionality, offering useful insights without overwhelming users with unnecessary complexity.

## Requirements

- C++17 compatible compiler
- Filesystem library support

## Building

To compile the program, use the following command:

```bash
g++ -std=c++17 da.cpp -o da
```

## Usage

```bash
./da [options] <directory>
```

### Options

- `-h, --help`: Show help message
- `-a, --all`: Include hidden files
- `-e, --exclude <dir>`: Directory to exclude (can be used multiple times)
- `-o, --output <file>`: Export results to CSV file
- `-t, --type <type>`: File type to include (can be used multiple times)
- `-s, --min-size <size>`: Minimum file size (e.g., 10K, 1M, 1.5G)
- `-S, --max-size <size>`: Maximum file size (e.g., 100M, 2G)
- `-j, --threads <count>`: Number of threads to use (default: all available cores)

### Example

```bash
da -a -e node_modules -t .cpp -t .h -s 1K -S 1M -j 4 -o results.csv /path/to/dir
```

This command will analyze the directory `/path/to/dir` using 4 threads, including hidden files, excluding the `node_modules` directory, only considering `.cpp` and `.h` files between 1KB and 1MB in size, and export the results to `results.csv`.

## Output

The program provides a colorized console output with statistics for each file type, including:

- File count
- Total size
- Average size
- Smallest file
- Largest file

Additionally, it shows the overall statistics for all analyzed files.

## CSV Export

When using the `-o` option, the program exports detailed results to a CSV file, including:

- File type
- File count
- Total size
- Size in human-readable format

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.
