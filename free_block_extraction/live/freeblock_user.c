#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>

#define DEVICE_PATH "/dev/ext4freeblk"

#define IOCTL_GET_BITMAP  _IOWR('f', 1, struct ext4_ioctl_req)
#define IOCTL_READ_BLOCK  _IOWR('f', 2, struct ext4_ioctl_req)

struct ext4_ioctl_req {
    char mount_path[128];
    unsigned long long block_nr;
    void *user_buf;

    unsigned long long blocks_count;
    unsigned int blocks_per_group;
    unsigned int groups_count;
    unsigned int block_size;
    unsigned long long free_blocks_count;
};

// 헬퍼 함수 - 에러 발생시
void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

// bitmap으로부터 free block 리스트 추출
int extract_free_blocks(uint8_t *bitmap_buf, unsigned int groups_count, 
                        unsigned int blocks_per_group, unsigned int block_size, 
                        unsigned long free_count, FILE *block_nr_file)
{
    uint8_t *one_bitmap_buf, *byte_val;
    bool bit;
    unsigned long block_nr;
    unsigned long long count = 0; // 본 함수에서 얻어오게 된 free block nr의 개수


    for (unsigned int g_id = 0; g_id < groups_count; g_id++) {
        one_bitmap_buf = bitmap_buf + g_id * block_size; // 한 비트맵의 처음을 가리키는 포인터
        for (unsigned int byte_id = 0; byte_id < block_size; byte_id++) {
            byte_val = one_bitmap_buf + byte_id; // 블록 비트맵 내부에서 바이트 하나를 가리키는 포인터
            for (int bit_id = 0; bit_id < 8; bit_id++) {
                bit = (*byte_val >> bit_id) & 1;
                if (!bit){
                    block_nr = blocks_per_group * g_id + byte_id * 8 + bit_id;
                    if(count + 1 > free_count) {
                        return 1;
                    }
                    fprintf(block_nr_file, "%lu\n", block_nr);
                }
            }
        }
    }
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <mount_path>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *mount_path = argv[1];
    int dev_fd = open(DEVICE_PATH, O_RDWR);
    if (dev_fd < 0) die("open");

    printf("[1] Getting bitmap + metadata ...\n");

    struct ext4_ioctl_req req = {0};
    strncpy(req.mount_path, mount_path, sizeof(req.mount_path)-1);

    // ioctl 구조체에 request value 담기
    size_t bitmap_size_guess = 1024 * 1024 * 512; // 최대 512MB 예비
    unsigned char *bitmap_buf = malloc(bitmap_size_guess);
    if (!bitmap_buf) {
        die("malloc bitmap_buf");
    }
    req.user_buf = bitmap_buf;

    // ioctl 보내기 전 struct size 확인
    printf("DEBUG: size of ioctl struct(before sending): %ld\n", sizeof(struct ext4_ioctl_req));

    // ioctl로 block_bitmap 받아오기
    if (ioctl(dev_fd, IOCTL_GET_BITMAP, &req) < 0) {
        die("IOCTL_GET_BITMAP");
    }
    printf("DEBUG: size of ioctl struct(after receiving): %ld\n", sizeof(struct ext4_ioctl_req));

    unsigned int block_size = req.block_size;
    unsigned long total_blocks_count = req.blocks_count;
    unsigned int groups_count = req.groups_count;
    unsigned int blocks_per_group = req.blocks_per_group;
    unsigned long free_blocks_count = req.free_blocks_count;
    size_t bitmap_size = (size_t)block_size * groups_count; // 비트맵들이 저장된 buffer 크기

    printf("  - block_size        = %u\n", block_size);
    printf("  - blocks_count      = %lu\n", total_blocks_count);
    printf("  - groups_count      = %u\n", groups_count);
    printf("  - bitmap_size       = %zu\n", bitmap_size);
    printf("  - free_blocks_count = %lu\n", free_blocks_count);

    // 버퍼 유효성 확인
    if (!bitmap_buf || block_size == 0 || groups_count == 0 || total_blocks_count == 0) {
        fprintf(stderr, "Error: Invalid bitmap data received\n");
        free(bitmap_buf);
        return EXIT_FAILURE;
    }
    
    if (free_blocks_count == 0) {
        fprintf(stderr, "There is no free block in %s\n", mount_path);
        free(bitmap_buf);
        return EXIT_SUCCESS;
    }

    // 간단히 첫 16바이트를 헥사로 출력해서 데이터가 들어왔는지 확인
    printf("  - bitmap sample (first 4 bytes): ");
    for (int i = 0; i < 4 && i < bitmap_size; i++) {
        printf("%02x ", bitmap_buf[i]);
    }
    printf("\n");

    printf("[2] Extracting free blocks ...\n");

    // 파일 저장 경로: ./
    const char fb_list_file_name[] = "./free_block_list.txt";
    const char fb_raw_file_name[] = "./free_blocks_raw.bin";

    FILE *fp_list = fopen(fb_list_file_name, "w");
    if (!fp_list) die("fopen free_block_list.txt");

    if(extract_free_blocks(bitmap_buf, groups_count, blocks_per_group, block_size, free_blocks_count, fp_list)) {
        fprintf(stderr, "Error: free blocks count is bigger than expectation");
        free(bitmap_buf);
        die("free block count");
    }

    free(bitmap_buf);
    fclose(fp_list);

    printf("[3] Reading free block data ...\n");

    // free block raw data bin 파일로 저장
    FILE *fp_raw = fopen(fb_raw_file_name, "w");
    fp_list = fopen(fb_list_file_name, "r");
    if(!fp_raw || !fp_list) die("fopen new file");
    fprintf(stderr, "file open ok\n");


    unsigned char *block_buf = malloc(block_size); // 블록 하나의 raw data 저장할 버퍼
    if (!block_buf) {
        die("malloc one block buf");
    }
    fprintf(stderr,"malloc one block_buf ok\n");

    unsigned long block_nr;
    unsigned long line_num = 0;
    while (fscanf(fp_list, "%lu", &block_nr) == 1) {
        line_num ++;

        if (block_nr >= total_blocks_count || block_nr == 0) {
            fprintf(stderr, "[WARN] Invalid block number %lu at line %lu\n", block_nr, line_num);
            continue;
        }

        memset(block_buf, 0, block_size);
        struct ext4_ioctl_req read_req = {0};
        strncpy(read_req.mount_path, mount_path, sizeof(read_req.mount_path)-1);
        read_req.block_nr = block_nr;
        read_req.user_buf = block_buf;

        if (ioctl(dev_fd, IOCTL_READ_BLOCK, &read_req) < 0) {
            fprintf(stderr, "Failed to read block%lu: %s\n", block_nr, strerror(errno));
            continue;
        }
        printf("getting block nr. %lu\n", block_nr);
        fwrite(block_buf, block_size, 1, fp_raw);
        printf("success writing on file\n");
    }

    free(block_buf);
    fclose(fp_list);
    fclose(fp_raw);
    close(dev_fd);

    printf("Done. Output files:\n");
    printf(" - %s\n", fb_list_file_name);
    printf(" - %s\n", fb_raw_file_name);
    return 0;
}
