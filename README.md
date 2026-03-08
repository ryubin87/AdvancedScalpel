# AdvancedScalpel
AdvancedScalpel with Free Block Extraction


## 1️⃣ Free Block Extraction in Ext4 Filesystem
✔️ Stage 1: Free Block-Focused Scope Reduction

### (Offline) Free Block Extraction

#### Go
```bash
cd ./free_block_extraction/offline
```

#### How to bulid
```bash
sudo apt update
sudo apt install e2fslibs-dev comerr-dev
gcc -Wall -g -o extraction_offline extraction_offline.c -lext2fs -lcom_err
```

#### How to use
- If measuring the extraction time
```bash
sudo time ./extraction_offline [Before.img] [After.img]
```
- If not
```bash
sudo ./extraction_offline [Before.img] [After.img]
```


### (Live) Free Block Extraction

#### Go
```bash
cd ./free_block_extraction/live
```
  
#### How to bulid
```bash
make -> insmod ext4_freeblock_module.ko
```

#### How to use
```bash
freeblock_user [Directory mounted on the Ext4 file system]
```


## 2️⃣ File Carving with AdvancedScalpel (advanced version of the scalpel)
✔️ Stage 2: Structure-Aware Validation

#### Go
```bash
cd ./advancedscalpel
```
  
#### How to bulid
- Linux:
```bash
make
```
- Win32:
```bash
make win32 [or mingw32-make win32]
```
- Mac OS:
```bash
make bsd
```

#### How to use
```bash
./scalpel -o [output_dir] [input_img]
```
- For more detailed usage instructions, please refer to the README.md file included separately in the advancedscalpel folder.
