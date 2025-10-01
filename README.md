# MiniVSFS

Tiny inode-based filesystem (MiniVSFS) with builder and adder tools in C.

---

## ✨ Features
- Superblock with checksum validation
- Inode & data bitmaps for block allocation
- First-fit allocation policy
- 12 direct block pointers per file
- Root-only directory (with `.` and `..` entries)
- Error handling for invalid inputs

---

## 📦 Repository structure
```
mini-vsfs/
├── src/
│   ├── mkfs_builder.c      # builds a new filesystem image
│   ├── mkfs_adder.c        # adds a file into the root directory (/)
│   └── common.h            # shared structures & helpers
├── examples/
│   ├── hello.txt           # sample test file
│   └── sample.bin
├── tests/
│   └── tests.sh            # automated test script
├── Makefile                # build/test/clean targets
└── README.md               # project documentation
```

---

## 🚀 Quick Start

### Build
```bash
make build
```

### Create a filesystem image
```bash
./mkfs_builder --image mini.img --size-kib 512 --inodes 256
```

### Add a file to the root directory (/)
```bash
./mkfs_adder --input mini.img --output mini_with_file.img --file examples/hello.txt
```

---

## 🖼️ Demo
![demo](https://user-images.githubusercontent.com/demo-gif-here.gif)  
*GIF: create image → add file → inspect with hexdump*

---

## 📊 Limits
- Max 12 data blocks per file (no indirect blocks)
- Only root (/) directory supported
- Single block for inode/data bitmap (limits total size/inodes)

---

## 🧩 Future Work
- Subdirectory support
- Indirect block pointers
- File permissions & ownership
- fsck-style integrity checker

---

## 🏗️ Tech Stack
- **Language**: C (C17)
- **Concepts**: Filesystem design, bitmaps, CRC checksums
- **Tools**: `xxd`, `hexdump` for debugging

---

## 📜 License
MIT License
