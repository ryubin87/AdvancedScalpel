#include <linux/device.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/mount.h>
#include <linux/path.h>
#include <linux/ioctl.h>
#include <linux/namei.h>
#include "ext4.h"

#define DEVICE_NAME "ext4freeblk"
#define CLASS_NAME "ext4freeblk_class"
#define IOCTL_GET_BITMAP  _IOWR('f', 1, struct ext4_ioctl_req)
#define IOCTL_READ_BLOCK  _IOWR('f', 2, struct ext4_ioctl_req)

struct ext4_ioctl_req {
    // 유저 공간 request
    char mount_path[128];  // 예: "/"
    __u64 block_nr;
    void __user *user_buf;

    // 커널 공간 response
    __u64 blocks_count;
    __u32 blocks_per_group;
    __u32 groups_count;
    __u32 block_size;
    __u64 free_blocks_count;
};

// 글로벌 device 관련
static struct file_operations fops;
static int major_num;
static struct class * ext4_class;
static struct device * ext4_device;

// super_block에 접근
static struct super_block* get_sb_from_path(const char *path)
{
    struct path p;
    struct super_block *sb;

    if (kern_path(path, LOOKUP_FOLLOW, &p))
        return NULL;
    sb = p.mnt->mnt_sb;

    // 파일시스템이 ext4가 아닌 경우 종료
    if (strcmp(sb->s_type->name, "ext4") != 0) {
        printk("Filesystem is not ext4 (found: %s)\n", sb->s_type->name);
        return NULL;
    }

    return sb;
}

// group_desc에 접근
static struct ext4_group_desc* get_gdb(struct super_block *sb, 
                                        ext4_group_t block_group, struct buffer_head **bh)
{
    struct ext4_sb_info *sbi = EXT4_SB(sb);
    ext4_group_t group_desc_block;
    int desc_per_block;
    struct buffer_head *bh_local;

    desc_per_block = EXT4_DESC_PER_BLOCK(sb);
    group_desc_block = block_group / desc_per_block;

    bh_local = sbi->s_group_desc[group_desc_block];
    if (!bh_local)
        return NULL;

    if (bh)
        *bh = bh_local;

    return (struct ext4_group_desc *)(
        (char *)bh_local->b_data +
        (block_group % desc_per_block) * EXT4_DESC_SIZE(sb)
    );
}



// 그룹 bitmap을 읽어 유저 공간으로 복사
static long get_block_bitmap(struct ext4_ioctl_req *req, void __user *user_req)
{
    struct ext4_group_desc *gdp;
    struct super_block *sb;
    struct ext4_sb_info *sbi;
    struct ext4_super_block *es;
    struct buffer_head *bh;
    struct ext4_ioctl_req tmp;
    char kpath[128];
    ext4_fsblk_t bitmap_block_nr;
    size_t bitmaps_size;
    void *bitmap, *kbuf, *ptr;
    int ret = 0;
    unsigned int block_size, blocks_per_group, groups_count, blocks_count, free_blocks_count;

    // ioctl 구조체 잘 얻어왔는지 확인
    /* printk(KERN_INFO "groups_count = %u, block_size = %u, total bitmap size = %llu\n",
       req->groups_count, req->block_size, 
       (unsigned long long)req->groups_count * req->block_size);
    */

    strcpy(kpath, req->mount_path);
    printk("kpath: %s", kpath);

    sb = get_sb_from_path(kpath);
    if (!sb) return -ENOENT;

    // 슈퍼 블록에서 필요한 데이터 가져오기
    sbi = EXT4_SB(sb);
    es = sbi->s_es;
    block_size = EXT4_BLOCK_SIZE(sb);
	blocks_count = ext4_blocks_count(EXT4_SB(sb)->s_es);
    blocks_per_group = EXT4_BLOCKS_PER_GROUP(sb);
    groups_count = ext4_get_groups_count(sb);
    free_blocks_count = ext4_free_blocks_count(es);

    // bitmap * groups_count 만큼의 버퍼 할당
    bitmaps_size = block_size * groups_count;
    kbuf = vzalloc(bitmaps_size);
    if (!kbuf) {
        printk(KERN_ERR "Failed to allocate bitmap buffer\n");
        return -ENOMEM;
    }
    ptr = kbuf;
    printk(KERN_INFO "before beginning: ptr offset=%td, block size=%d\n", ptr - kbuf, block_size);

    // 그룹 디스크립터 순회하면서 block bitmap을 가져와서 버퍼에 저장
    for (unsigned int i = 0; i < groups_count; i++) {
        gdp = get_gdb(sb, i, NULL);

        bitmap_block_nr = le32_to_cpu(gdp->bg_block_bitmap_lo) |
		(EXT4_DESC_SIZE(sb) >= EXT4_MIN_DESC_SIZE_64BIT ? (ext4_fsblk_t)le32_to_cpu(gdp->bg_block_bitmap_hi) << 32 : 0);

        /* for debugging */
        // printk(KERN_INFO "group %d's block nr: %llu", i, bitmap_block_nr);

        bh = sb_bread(sb, bitmap_block_nr);
        if (IS_ERR(bh)) {
            vfree(kbuf);
            return PTR_ERR(bh);
        }

        memcpy(ptr, bh->b_data, block_size);
        ptr += block_size;
        if (ptr - kbuf > bitmaps_size) { printk("OVERRUN"); break; }

        brelse(bh);
        // printk(KERN_INFO "ptr offset=%td, bitmaps_size=%zu", ptr - kbuf, bitmaps_size);
        // printk(KERN_INFO "%d/%d copy memory ok!", i+1, groups_count);
    }

    // bitmap 수집 완료 후 확인용
    /*
    for (int i = 0; i < 16 && i < bitmaps_size; i++) {
        printk(KERN_CONT "%02x ", ((uint8_t *)kbuf)[i]);
    }
    printk(KERN_CONT "\n");
    */

    if (copy_to_user(req->user_buf, kbuf, bitmaps_size)) {
        return -EFAULT;
    }

    tmp = *req;
    tmp.blocks_count = blocks_count;
    tmp.blocks_per_group = blocks_per_group;
    tmp.groups_count = groups_count;
    tmp.block_size = block_size;
    tmp.free_blocks_count = free_blocks_count;
    tmp.user_buf = NULL;

    if (copy_to_user(user_req, &tmp, sizeof(tmp))) {
        ret = -EFAULT;
    }

    vfree(kbuf);
    return ret;
}

// 블록의 raw data를 유저 공간으로 복사
static long read_block_data(struct ext4_ioctl_req *req, void __user *user_req)
{
    struct super_block *sb;
    struct buffer_head *bh;
    int ret = 0;

    // 블록이 잘 추출되고 있는지 확인
    // printk(KERN_INFO "from %s: extracting block nr %d", req->mount_path, req->block_nr);

    sb = get_sb_from_path(req->mount_path);
    if (!sb) return -ENOENT;

    bh = sb_bread(sb, req->block_nr);
    if (IS_ERR(bh)) return -EIO;

    // for debugging
    // printk("size: %lu raw: %02x", bh->b_size, ((uint8_t *)bh->b_data));
    if (copy_to_user(req->user_buf, bh->b_data, bh->b_size))
        ret = -EFAULT;

    brelse(bh);
    return ret;
}

static long ext4freeblk_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct ext4_ioctl_req req;

    if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
        return -EFAULT;

    // ioctl 받은 후 struct size 확인
    // printk(KERN_INFO "size of ioctl struct: %ld\n", sizeof(struct ext4_ioctl_req));

    switch (cmd) {
    case IOCTL_GET_BITMAP:
        return get_block_bitmap(&req, (void __user *)arg);
    case IOCTL_READ_BLOCK:
        return read_block_data(&req, (void __user *)arg);
    default:
        return -ENOTTY;
    }
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = ext4freeblk_ioctl,
};

static int __init ext4freeblk_init(void)
{
    major_num = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_num < 0) {
        printk(KERN_ERR "Failed to register device\n");
        return major_num;
    }

    ext4_class = class_create(CLASS_NAME);
    if (IS_ERR(ext4_class)) {
        printk(KERN_ERR "Failed to create class\n");
        unregister_chrdev(major_num, DEVICE_NAME);
        return PTR_ERR(ext4_class);
    }

    ext4_device = device_create(ext4_class, NULL, MKDEV(major_num, 0), NULL, DEVICE_NAME);
    if (IS_ERR(ext4_device)) {
        class_destroy(ext4_class);
        return PTR_ERR(ext4_device);
    }
    printk(KERN_INFO "ext4_freeblock module loaded: /dev/%s\n", DEVICE_NAME);
    return 0;
}

static void __exit ext4freeblk_exit(void)
{
    device_destroy(ext4_class, MKDEV(major_num, 0));
    class_destroy(ext4_class);
    unregister_chrdev(major_num, DEVICE_NAME);
    printk(KERN_INFO "ext4_freeblock module unloaded\n");
}

module_init(ext4freeblk_init);
module_exit(ext4freeblk_exit);


MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Youjung Heo");
MODULE_DESCRIPTION("Ext4 free block bitmap reader");