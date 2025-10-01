# MiniVSFS

![C](https://img.shields.io/badge/language-C-blue)
![Build](https://img.shields.io/badge/build-passing-brightgreen)
![License](https://img.shields.io/badge/license-MIT-lightgrey)

Tiny inode-based filesystem (MiniVSFS) with builder and adder tools in C.

---

## ✨ Features

* Superblock with checksum validation
* Inode & data bitmaps for allocation
* First-fit allocation policy
* 12 direct block pointers per file
* Root-only directory (with `.` and `..` entries)
* Error handling for invalid inputs

---

## 📦 Repository structure

```
mini-vsfs/
├── src/
│   ├── mkfs_builder.c   # builds a new filesystem image
│   └── mkfs_adder.c     # adds a file into the root directory (/)
├── tests/
│   └── tests.sh         # automated test script
├── examples/
│   └── hello.txt        # sample test file
├── Makefile             # build/test/clean targets
├── README.md            # project documentation
└── LICENSE              # MIT License
```

---

## 🚀 Quick Start (Ubuntu)

### Prerequisites

```bash
sudo apt update
sudo apt install -y git build-essential xxd
```

### Clone & Build

```bash
git clone https://github.com/withishtiaq/mini-vsfs.git
cd mini-vsfs
make build
```

### Create a filesystem image

```bash
./mkfs_builder --image mini.img --size-kib 512 --inodes 256
```

### Add a file to the root directory (/)

```bash
echo "hello, mini-vsfs" > examples/hello.txt
./mkfs_adder --input mini.img --output mini2.img --file examples/hello.txt
```

### Inspect with xxd

```bash
# Superblock (block 0)
xxd -g 4 -l 128 -s $((0*4096)) mini2.img | head

# Root inode (block 3)
xxd -g 4 -l 128 -s $((3*4096)) mini2.img | head
```

### Run tests

```bash
chmod +x tests/tests.sh
make test
```

Expected output: `[tests] OK ✅`

---

## 🖼️ Demo

![demo](link-to-your-demo.gif)
*GIF: create image → add file → inspect with hexdump*

---

## 📊 Limits

* Max 12 data blocks per file (no indirect blocks)
* Only root (/) directory supported
* Single block for inode/data bitmap (hard limits)

---

## 🧩 Future Work

* Subdirectory support
* Indirect block pointers
* File permissions & ownership
* fsck-style integrity checker

---

## 🏗️ Tech Stack

* **Language:** C (C17)
* **Concepts:** Filesystem design, inodes, bitmaps, CRC checksums
* **Tools:** gcc, make, xxd, hexdump

---

## 📜 License

MIT License
