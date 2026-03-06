# AdvancedScalpel
AdvancedScalpel with Free Block Extraction

## Free Block Extraction in Ext4 Filesystem

### (Offline) Free Block Extraction
* ./free_block_extraction/offline


**- How to bulid**
```bash
sudo apt update
```
```bash
sudo apt install e2fslibs-dev comerr-dev
```
```bash
gcc -Wall -g -o extraction_offline extraction_offline.c -lext2fs -lcom_err
```

**- How to use**
  - If measuring the extraction time
```bash
sudo time ./extraction_offline <Before.img> <After.img>
```
  - If not
```bash
sudo ./extraction_offline <Before.img> <After.img>
```

### (Live) Free Block Extraction
* ./free_block_extraction/live

  
**- How to bulid**
```bash
make -> insmod ext4_freeblock_module.ko
```

**- How to use**
```bash
...
```
