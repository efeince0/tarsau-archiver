# TARSAU - Linux/Unix System Programming Archiving Tool

[![C](https://img.shields.io/badge/C-99%25-blue)](https://en.wikipedia.org/wiki/C_(programming_language))
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20Unix%20%7C%20Windows-blue.svg)](.)

A lightweight, non-compressing archiving tool for ASCII text files, similar to tar/zip but without compression. Developed as a system programming course project.

## 🎯 Features

- **Archive Creation** (`-b` mode): Bundle up to 32 ASCII text files into a single `.sau` archive
- **Archive Extraction** (`-a` mode): Extract files from `.sau` archives with original permissions preserved
- **Permission Preservation**: Stores and restores file permissions in octal format
- **Custom Format**: Efficient `.sau` archive format with metadata and content sections
- **Size Limits**: Supports up to 32 files with 200MB total size limit
- **ASCII Validation**: Ensures only valid 7-bit ASCII text files are archived
- **Security**: Path traversal protection and comprehensive error handling
- **Memory Safety**: Full Valgrind testing for memory leak detection

## 📋 Requirements

- **OS**: Linux, Unix, or Windows (MinGW)
- **Compiler**: GCC
- **Standard**: POSIX.1-2008 compatible
- **Build Tool**: GNU Make

### Compilation

```bash
make          # Compile the program
make clean    # Remove build artifacts
make test     # Run integration tests
make check    # Run Valgrind memory tests
```

## 🚀 Quick Start

### Archive Files

```bash
# Create archive with default name (a.sau)
./tarsau -b file1.txt file2.txt file3.txt

# Create archive with custom name
./tarsau -b file1.txt file2.txt -o myarchive.sau
```

### Extract Files

```bash
# Extract to current directory
./tarsau -a archive.sau

# Extract to specific directory
./tarsau -a archive.sau /path/to/output
```

## 📦 Archive Format (.sau)

The `.sau` format consists of two sections:

```
┌────────────────────────────────────────┐
│ SECTION 1: Metadata (Organization)    │
├────────────────────────────────────────┤
│ [0-9]    : 10-byte section size (ASCII)│
│ [10+]    : |filename,permissions,size|│
│            records separated by pipes  │
└────────────────────────────────────────┘
┌────────────────────────────────────────┐
│ SECTION 2: File Contents               │
├────────────────────────────────────────┤
│ File data concatenated sequentially    │
│ Order matches metadata records         │
└────────────────────────────────────────┘
```

## 🧪 Testing

Run the comprehensive test suite:

```bash
make test
```

Tests include:
- ✓ Archiving multiple files
- ✓ Extracting with permission preservation
- ✓ ASCII file validation
- ✓ Corrupted archive detection
- ✓ Default output naming
- ✓ Extraction to specified directories

Check for memory leaks:

```bash
make check
```

## 📝 Usage Examples

### Example 1: Simple Archiving

```bash
$ echo "Hello, World!" > file1.txt
$ echo "Test content" > file2.txt
$ ./tarsau -b file1.txt file2.txt -o test.sau
Arşiv başarıyla oluşturuldu: test.sau  (2 dosya, 0.03 KB)
```

### Example 2: Extraction with Permissions

```bash
$ ./tarsau -a test.sau extracted/
Çıkarıldı: extracted/file1.txt    izin: 0644  boyut: 13 bayt
Çıkarıldı: extracted/file2.txt    izin: 0644  boyut: 12 bayt
Arşiv başarıyla açıldı: 2 dosya çıkarıldı.
```

### Example 3: Multiple Files with Different Permissions

```bash
$ chmod 755 important.txt
$ chmod 600 secret.txt
$ ./tarsau -b important.txt secret.txt notes.txt -o mydata.sau
Arşiv başarıyla oluşturuldu: mydata.sau  (3 dosya, 0.15 KB)

$ ./tarsau -a mydata.sau backup/
Çıkarıldı: backup/important.txt   izin: 0755  boyut: 1024 bayt
Çıkarıldı: backup/secret.txt      izin: 0600  boyut: 512 bayt
Çıkarıldı: backup/notes.txt       izin: 0644  boyut: 256 bayt
Arşiv başarıyla açıldı: 3 dosya çıkarıldı.

$ ls -l backup/
-rwxr-xr-x important.txt
-rw------- secret.txt
-rw-r--r-- notes.txt
```

### Example 4: Archive Verification

```bash
# Create archive
$ ./tarsau -b doc1.txt doc2.txt -o backup.sau

# Inspect archive structure (first 100 bytes)
$ head -c 100 backup.sau

# Extract and verify
$ ./tarsau -a backup.sau extracted/
$ diff doc1.txt extracted/doc1.txt && echo "✓ Files match perfectly"
```

### Example 5: Batch Processing

```bash
# Archive all .txt files in a directory
$ ./tarsau -b *.txt -o all_docs.sau

# Extract to a new backup directory
$ mkdir -p backups/2026-05-24
$ ./tarsau -a all_docs.sau backups/2026-05-24/
```

## ⚠️ Limitations & Requirements

- **ASCII Only**: Only accepts 7-bit ASCII text files (no UTF-8, binary, or Unicode)
- **File Limit**: Maximum 32 files per archive
- **Size Limit**: Total archive size cannot exceed 200MB
- **Text Format**: Binary files are automatically rejected

## 🔒 Security Features

- **Path Traversal Protection**: Extracts only to specified directory using basename
- **Buffer Overflow Protection**: Safe string handling with `strncpy` and `snprintf`
- **Input Validation**: Comprehensive checks for file types, sizes, and formats
- **Memory Management**: Proper allocation and deallocation with Valgrind verification

## 📄 File Structure

```
tarsau/
├── tarsau.c          # Main implementation (699 lines)
├── Makefile          # Build configuration with test targets
├── README.md         # This file
├── .gitignore        # Git ignore rules
└── RAPOR.md          # Detailed Turkish project report
```

## 🛠️ Implementation Details

### Key Functions

- `arsivle()` - Archive creation with metadata organization
- `arsiv_ac()` - Archive extraction with permission restoration
- `metin_dosyasi_mi()` - ASCII file validation
- `organizasyon_bolumu_olustur()` - Metadata serialization
- `organizasyon_bolumunu_ayristir()` - Metadata deserialization

### Memory Efficiency

- Buffered I/O with 8KB buffers
- Dynamic memory allocation for metadata
- Proper resource cleanup on errors

## 📚 Project Context

This project was developed as part of a **Computer Engineering System Programming** course (2025-2026 Spring Semester). It demonstrates:

- POSIX system programming (file operations, permissions)
- C memory management and error handling
- Data serialization and binary formats
- Build automation with Makefiles
- Software testing and quality assurance

## 🇹🇷 Language Note

The project includes Turkish comments and error messages. The interface and documentation are provided in both Turkish and English.

## 📄 License

This project is provided as educational material.

## 📖 Documentation

- **RAPOR.md** - Comprehensive Turkish project report with test results
- **RAPOR.docx** - Project report in Microsoft Word format
- **Code Comments** - Inline documentation in the source code

## 🤝 Contributing

This is an educational project. For questions or suggestions, feel free to open an issue or contact the author.

## 📧 Contact

**Author**: efeince0  
**Platform**: GitHub

---

**Last Updated**: May 23, 2026  
**Status**: Complete & Tested ✓
