#include <stdio.h>
#include <stdlib.h>
#include <ext2fs/ext2fs.h>
#include <unistd.h>
#include <fcntl.h>

static ext2fs_block_bitmap open_filesystem(char *device, int open_flags, blk_t superblock, blk_t blocksize, char *data_filename);

int main(int argc, char *argv[]) {

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <device or image file> <output image file>\n", argv[0]);
        return 1;
    }

    ext2fs_block_bitmap bmap = NULL;
    
    const char *dst = argv[2];

    bmap = open_filesystem(argv[1], 0, 0, 0, NULL);

    if (bmap == NULL) {
        fprintf(stderr, "Failed to open filesystem or read block bitmap\n");
        return 1;
    }

    printf("success!!!\n");
    
    ext2_filsys fs = bmap -> fs;
    
    printf("Free blocks: %llu\n", ext2fs_free_blocks_count(fs->super));
    
    int fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
    	perror("open error");
    	return 1;
    }
    
    char *buf = malloc(fs -> blocksize);
    if (!buf) {
    	printf("buf malloc error");
    	close(fd);
    	return 1;
    }
    
    long copied_blocks = 0;
    long count_blocks=0;
    
    for (blk64_t b = fs->super->s_first_data_block; b < ext2fs_blocks_count(fs->super); b++) {
    	count_blocks++;
    	if (!ext2fs_test_block_bitmap2(bmap, b)) { // test free block
    		errcode_t retval = io_channel_read_blk64(fs->io, b, 1, buf);
    		if (retval) {
    			printf("error read block");
    			continue;
    		}
    		if (write(fd, buf, fs->blocksize) != fs->blocksize) {
    			printf("write error");
    			break;
    		}
    		copied_blocks++;
    	}
    }
    
    printf("copied_blocks %ld\n", copied_blocks);
    printf("count_blocks %ld\n", count_blocks);
    
    printf("\n\n");
   
    
    free(buf);
    close(fd);
    ext2fs_close(fs);
    
    return 0;
}



static ext2fs_block_bitmap open_filesystem(char *device, int open_flags, blk_t superblock, blk_t blocksize, char *data_filename)
{
    ext2_filsys current_fs = NULL;
    int retval;
    ext2fs_block_bitmap bmap = NULL;
    io_channel data_io = 0;

    if (superblock != 0 && blocksize == 0) {
        fprintf(stderr, "if you specify the superblock, you must also specify the block size\n");
        return NULL;
    }

    if (data_filename) {
        if ((open_flags & EXT2_FLAG_IMAGE_FILE) == 0) {
            fprintf(stderr, "The -d option is only valid when reading an e2image file\n");
            return NULL;
        }
        retval = unix_io_manager->open(data_filename, 0, &data_io);
        if (retval) {
            fprintf(stderr, "%s while opening data source\n", data_filename);
            return NULL;
        }
    }



    if (open_flags & EXT2_FLAG_RW) {
        open_flags &= ~EXT2_FLAG_RW;
    }

    retval = ext2fs_open(device, open_flags, superblock, blocksize, unix_io_manager, &current_fs);
    if (retval) {
        fprintf(stderr, "%s Error %d while opening filesystem\n", device, retval);
        return NULL;
    }

    retval = ext2fs_read_block_bitmap(current_fs);
    if (retval) {
        fprintf(stderr, "%s Error %d while reading block bitmap\n", device, retval);
        goto errout;
    }

    if (data_io) {
        retval = ext2fs_set_data_io(current_fs, data_io);
        if (retval) {
            fprintf(stderr, "%s Error %d while setting data source\n", device, retval);
            goto errout;
        }
    }

    bmap = current_fs->block_map;

#ifdef EXT2_FLAG_64BITS
    //ext2fs_clear_generic_bmap(bmap);
#else
    //ext2fs_clear_block_bitmap(bmap);
#endif
    return bmap;

errout:
    retval = ext2fs_close(current_fs);
    if (retval)
        fprintf(stderr, "%s Error %d while trying to close filesystem\n", device, retval);
    return NULL;
}
